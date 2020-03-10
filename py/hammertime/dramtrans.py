#!/usr/bin/env python3

import os
import sys
import struct
import ctypes
import ctypes.util
import functools


HASH_FN_CNT = 6
_native = None

@functools.total_ordering
class DRAMAddr(ctypes.Structure):
    _fields_ = [('bank', ctypes.c_uint64),
                ('row', ctypes.c_uint64),
                ('col', ctypes.c_uint64)]
    
    def __str__(self):
        return 'b{0.bank:02d}.r{0.row:06d}.c{0.col:04d}'.format(self)
    
    def __repr__(self):
        return self.__str__()
#    def __repr__(self):
#        return '{0}(b={1.bank},r={1.row},c={1.col})'.format(type(self).__name__, self)

    def __eq__(self, other):
        if isinstance(other, DRAMAddr):
            return self.numeric_value == other.numeric_value
        else:
            return NotImplemented

    def __lt__(self, other):
        if isinstance(other, DRAMAddr):
            return self.numeric_value < other.numeric_value
        else:
            return NotImplemented

    def __hash__(self):
        return self.numeric_value

    def __len__(self):
        return len(self._fields_)

    def __getitem__(self, key):
        if isinstance(key, int):
            return getattr(self, self._fields_[key][0])
        elif isinstance(key, slice):
            start, stop, step = key.indices(len(self._fields_))
            return tuple(getattr(self, self._fields_[k][0]) for k in range(start, stop, step))
        else:
            raise TypeError('{} object cannot be indexed by {}'.format(type(self).__name__, type(key).__name__))

    def same_bank(self, other):
        return  self.bank == other.bank

    @property
    def numeric_value(self):
        return (self.col + (self.row << 16) + (self.bank << 32))

    def __add__(self, other):
        if isinstance(other, DRAMAddr):
            return type(self)(
                self.bank + other.bank,
                self.row + other.row,
                self.col + other.col
            )
        else:
            return NotImplemented

    def __sub__(self, other):
        if isinstance(other, DRAMAddr):
            return type(self)(
                self.bank - other.bank,
                self.row - other.row,
                self.col - other.col
            )
        else:
            return NotImplemented


class _AddrFns(ctypes.Structure):
    _fields_ = [("lst", ctypes.c_uint64* HASH_FN_CNT),
                ("len", ctypes.c_uint64)]


class _DRAMLayout(ctypes.Structure):
    _fields_ = [("h_fns", _AddrFns),
                ("row_mask", ctypes.c_uint64),
                ("col_mask", ctypes.c_uint64)]

    def __init__(self, upack):
        self.h_fns.lst = (ctypes.c_uint64*6) (*[0x2040,0x44000,0x88000,0x110000,0x220000,0x00]) 
        self.h_fns.len = 5
        self.row_mask = 0xffffc0000
        self.col_mask = ((1<<13)-1)
#        self.h_fns.lst = upack[0:HASH_FN_CNT]
#        self.h_fns.len = upack[HASH_FN_CNT]
#        self.row_mask = upack[HASH_FN_CNT+1]
#        self.col_mask = upack[HASH_FN_CNT+2]

    @property
    def num_banks(self):
        return 1<<self.AddrFns.len
    
    def get_dram_row(self, p_addr): 
        return (p_addr & self.row_mask) >> _native.ctzl(self.row_mask)

    def get_dram_row(self, p_addr): 
        return (p_addr & self.col_mask) >> _native.ctzl(self.col_mask)


class MemorySystem(ctypes.Structure):
    _fields_ = [('mem_layout', _DRAMLayout)]
   
    def __init__(self):
        if _native == None:
            load_native_funcs()

        return super().__init__()
    
    def load(self, s):
        DRAMLAyout_fmt = f"{HASH_FN_CNT}QQQQ"
        self.mem_layout = _DRAMLayout(struct.unpack(DRAMLAyout_fmt, s))

    def load_file(self, fname):
        with open(fname, 'rb') as f:
            return self.load(f.read())

    def num_banks(self):
        return self.mem_layout.num_banks


    def resolve(self, p_addr):
        mem = self.mem_layout 
        d_addr = DRAMAddr(0,0,0) 
        for i in range(mem.h_fns.len):
            d_addr.bank |= (_native.parity(p_addr & mem.h_fns.lst[i]) << i)
        d_addr.row = mem.get_dram_row(p_addr) 
        d_addr.col = mem.get_dram_col(p_addr)
        
        return d_addr


    def resolve_reverse(self, d_addr):
        mem = self.mem_layout
        p_addr = (d_addr.row << _native.ctzl(mem.row_mask)) 
        p_addr |= (d_addr.col << _native.ctzl(mem.col_mask))
        for i in range(mem.h_fns.len):
            masked_addr = p_addr & mem.h_fns.lst[i]     
            if _native.parity(masked_addr) == ((d_addr.bank >> i) & 1):
                continue
# else flip a bit of the address so that the address respects the dram h_fn
# that is get only bits not affecting the row.
            h_lsb = _native.ctzl((mem.h_fns.lst[i]) & ~(mem.col_mask) & ~(mem.row_mask))
            p_addr ^= 1<<h_lsb
                
        return p_addr 




def load_native_funcs():
    global _native
    path = os.path.dirname(os.path.abspath(__file__))
    _native = ctypes.CDLL(os.path.join(path, "_native.so"))
    _native.parity.restype = ctypes.c_uint64
    _native.parity.argtypes= [ctypes.c_uint64]
    _native.ctzl.restype = ctypes.c_uint64
    _native.ctzl.argtypes= [ctypes.c_uint64]
