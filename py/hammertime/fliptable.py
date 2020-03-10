#!/usr/bin/env python3
# -*- coding: UTF-8 -*-
# Copyright (c) 2016 Andrei Tatar
# Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""Module providing utilities for working with profile output and fliptables."""

import re
import pprint as pp

from collections import namedtuple

from hammertime.dramtrans import DRAMAddr

RE_DICT = re.compile("(?P<key>[-\d\w]+)\s*:\s*(?P<val>[-.\d\w]+)")
#ADDR_RE = re.compile("r(?P<row>\d+)\.bk(?P<bk>\d+)(\.col(?P<col>\d+))?(\.p(?P<prob>\d+))?")
ADDR_RE = re.compile("r(?P<row>\d+)\.bk(?P<bank>\d+)(\.col(?P<col>\d+))?")
#ADDR_FMT = r'\((\w+)\s+(\w+)\s*(\w+)?\)'
#ADDR_RE = re.compile(ADDR_FMT)
# 87,c7,r16447.bk18.col3994 
BFLIP_FMT = r'(?P<exp>\w{2}),(?P<got>\w{2}),(?P<addr>[.\w\d]+)'
BFLIP_RE = re.compile(BFLIP_FMT)
ADDR_TRG_RE = re.compile("b(?P<bk>[\d\w]+)\.r(?P<row>[\d\w]+)")
#VICT_FMT = r'{} (?:{}\s?)+'.format(ADDR_FMT, BFLIP_FMT)
#VICT_RE = re.compile(VICT_FMT)


class Corruption(namedtuple('Corruption', ['addr', 'got', 'exp'])):
    def __str__(self):
        return '({0.addr}|{0.got:02x}|{0.exp:02x})'.format(self)

    def __repr__(self):
        return self.__str__() 

    def to_flips(self):
        flips = []
        pup = ~self.exp & self.got & 0xff
        pdn = self.exp & ~self.got & 0xff
        bit = 0
        while pup:
            mask = 1 << bit
            if (pup & mask):
                flips.append(Flip(self.addr, bit, True))
                pup &= ~mask
            bit += 1
        bit = 0
        while pdn:
            mask = 1 << bit
            if (pdn & mask):
                flips.append(Flip(self.addr, bit, False))
                pdn &= ~mask
            bit += 1
        return flips


class Flip(namedtuple('Flip', ['addr', 'bit', 'pullup'])):
    def to_corruption(self, og=None):
        fmask = 1 << (self.bit % 8)
        if og is None:
            pat = 0 if self.pullup else 0xff
        else:
            pat = og 
        val = pat | fmask if self.pullup else pat & ~fmask
        return Corruption(addr=self.addr, got=val, exp=pat)

    def to_physmem(self, msys):
        return type(self)(msys.resolve_reverse(self.addr), self.bit, self.pullup)


Diff = namedtuple('Diff', ['self_only', 'common', 'other_only'])


class Attack(namedtuple('Attack', ['targets', 'flips'])):
    def diff(self, other):
        if not isinstance(other, type(self)):
            raise TypeError('Attack instance expected for diff')
        elif not self.targets == other.targets:
            raise ValueError('Cannot diff attacks with different targets')
        else:
            return Diff(
                type(self)(self.targets, self.flips - other.flips),
                type(self)(self.targets, self.flips & other.flips),
                type(self)(self.targets, other.flips - self.flips)
            )

    def merge(self, other):
        if not isinstance(other, type(self)):
            raise TypeError('Attack instance expected for merge')
        elif not self.targets == other.targets:
            raise ValueError('Cannot merge attacks with different targets')
        else:
            return type(self)(self.targets, self.flips | other.flips)

    def to_corruptions(self, pat=None):
        return ((x.addr, x.to_corruption(pat)) for x in self)

    def to_physmem(self, msys):
        return type(self)(
            targets=tuple(msys.resolve_reverse(x) for x in self.targets),
            flips={x.to_physmem(msys) for x in self.flips}
        )

    def __iter__(self):
        return iter(sorted(self.flips))

    @classmethod
    def __is_flip_far(cls, targets, victim):
        close = False
        for t in targets:
            if abs(t.row - victim.row) == 1:
                close = True
        if not close:
            print("Found flip far away from target row")
            print(targets)
            print(victim)

    @classmethod
    def decode_line(cls, line, msys=None):
        def parse_addr(s):
            res = ADDR_RE.match(s)
            if res == None:
                raise Exception("Couldn't parse")
            return DRAMAddr(**({k:int(v) for (k,v) in res.groupdict().items() if v != None}))
        
        def parse_addr_trg(s):
            res = ADDR_TRG_RE.match(s)
            if res == None:
                raise Exception("Couldn't parse")
            return DRAMAddr(**({k:int(v) for (k,v) in res.groupdict().items() if v != None}))
                       
        targ, vict, *o = line.split(':')
        flips = set()
        targets = [parse_addr(s) for s in line.split('/')]

        for x in BFLIP_RE.finditer(vict):
            dct = x.groupdict()
            dct['addr'] = parse_addr(dct['addr'])
            dct['got'] = int(dct['got'], 16) 
            dct['exp'] = int(dct['exp'], 16)
            cls.__is_flip_far(targets, dct['addr'])
            flips.update(Corruption(**(dct)).to_flips())
            
#            vaddr = DRAMAddr(*(int(v, 16) for v in x.group(*range(1,4)) if v is not None))
#            vcorr = [Corruption(*(int(v, 16) for v in y.groups())) for y in BFLIP_RE.finditer(x.group(0))]
#            for corr in vcorr:
#                flips.update(corr.to_flips(vaddr, msys))
        return cls(
            targets= targets,
            flips=flips
        )

    def encode(self, patterns=None):
        if patterns is None:
            patterns = [[0xff], [0]]
        corrs = [
            [x for x in self.to_corruptions(pat) if x[1].exp != x[1].got]
            for pat in patterns
        ]
        tstr = ' '.join(str(x) for x in self.targets) + ' : '
        return '\n'.join(
            tstr +
            ' '.join(' '.join(str(x) for x in c) for c in corr)
            for corr in corrs
        )


def decode_lines(lineiter):
    curatk = None
    for line in lineiter:
        atk = Attack.decode_line(line)
        if curatk is None:
            curatk = atk
        else:
            try:
                curatk = curatk.merge(atk)
            except ValueError:
                yield curatk
                curatk = atk
    if curatk is not None:
        yield curatk


class Parameters(namedtuple("Parameters", ['h_cfg', 'd_cfg', 'h_rows', 'h_rounds', 'd_base'])):
    """"""
    @classmethod
    def parse_params(cls, string):
        matches = RE_DICT.findall(string)
        res = {}
        for m in matches:
            res[m[0]] = m[1]

        return cls(*res.values())



class Fliptable:
    def __init__(self, attacks):
        self.attacks=attacks

    def __eq__(self, other):
        if isinstance(other, type(self)):
            return self.attacks == other.attacks
        else:
            return NotImplemented

    def __len__(self):
        return len(self.attacks)

    def __iter__(self):
        return iter(self.attacks)

    def __str__(self):
        return '\n'.join(atk.encode() for atk in self)

    def __add__(self, other):
        return Fliptable(self.attacks + other.attacks)

    def diff(self, other):
        if not isinstance(other, type(self)):
            raise ValueError('Fliptable required for diff')
        uself = []
        uother = []
        common = []

        satks = iter(self)
        oatks = iter(other)
        sa = next(satks, None)
        oa = next(oatks, None)
        while sa is not None or oa is not None:
            # Degenerate cases
            if sa is None:
                uother.append(oa)
                uother.extend(oatks)
                break
            if oa is None:
                uself.append(sa)
                uself.extend(satks)
                break
            try:
                adiff = sa.diff(oa)
                if adiff.self_only.flips:
                    uself.append(adiff.self_only)
                common.append(adiff.common)
                if adiff.other_only.flips:
                    uother.append(adiff.other_only)
                sa = next(satks, None)
                oa = next(oatks, None)
            except ValueError:
                if sa.targets < oa.targets:
                    uself.append(sa)
                    sa = next(satks, None)
                elif sa.targets > oa.targets:
                    uother.append(oa)
                    oa = next(oatks, None)
        FT = type(self)
        return Diff(FT(uself), FT(common), FT(uother))

    def to_physmem(self, msys):
        return type(self)([x.to_physmem(msys) for x in self])

    @classmethod
    def load_file(cls, fname):
        with open(fname, 'r') as f:
#            params_line = f.readline().remove("#") # read the first line which contains the parameters of the flip table
#            params = Parameters.parse_params(params_line)
#            flips = pd.read_csv(f)
#            attacks = attacks_generator()
            return cls(list(decode_lines(f)))

    def save_file(self, fname):
        with open(fname, 'w') as f:
            f.write(str(self))
