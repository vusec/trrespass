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

import math
import itertools
import functools
from collections import namedtuple

from hammertime import fliptable
from hammertime import dramtrans 
import sys

scan_time = 60000 #ns to scan a row (terrible implementation)
fill_time = 1500 #ns to fill a row


class PageBitFlip(namedtuple('PageBitFlip', ['byte_offset', 'mask'])):
    """Represents a byte with one or more flipped bits at a particular offset within a page"""

class VictimPage(namedtuple('VictimPage', ['pfn', 'pullups', 'pulldowns'])):
    """Represents the results of one rowhammer attack on one particular physical page"""


class ExploitModel:

    def check_page(self, vpage):
        raise NotImplementedError()

    def check_attack(self, attack):
        for vpage in attack:
            if self.check_page(vpage):
                yield vpage.pfn

    def check_attacks(self, attacks):
        for atk in attacks:
            yield tuple(self.check_attack(atk))


def _map_attack(atk, msys, pagesize=0x1000):
    pfnof = lambda x: x.addr // pagesize
    byteof = lambda x: x.addr % pagesize + x.bit // 8
    vict_pages = []
    for pfn, pflips in itertools.groupby(atk.to_physmem(msys), pfnof):
        ups = set()
        downs = set()
        for byte, iflips in itertools.groupby(pflips, byteof):
            bflips = list(iflips)
            pup = functools.reduce(lambda x,y: x | y, (1 << (x.bit % 8) for x in bflips if x.pullup), 0)
            pdn = functools.reduce(lambda x,y: x | y, (1 << (x.bit % 8) for x in bflips if not x.pullup), 0)
            if pup:
                ups.add(PageBitFlip(byte, pup))
            if pdn:
                downs.add(PageBitFlip(byte, pdn))
        if ups or downs:
            vict_pages.append(VictimPage(pfn, ups, downs))
    return vict_pages


class BaseEstimator:
    def __init__(self):
        self.clear()

    def iter_attacks(self):
        """
        Return an iterator over possible attacks

        An attack consists of a sequence of VictimPages that have bits flipped
        as part of a single Rowhammer attack.
        """
        raise NotImplementedError()

    def run_exploit(self, model):
        self.results = list(model.check_attacks(self.iter_attacks()))

    def clear(self):
        self.results = []

    def print_stats(self):
        if self.results:
            succ = sum(1 for x in self.results if x)
            npages = sum(len(x) for x in self.results if x)
            prop = succ / len(self.results)
            print('{} total attacks (over {} KiB), of which {} successful ({:5.1f} %)'.format(
                len(self.results), len(self.results) * 8, succ, 100.0 * prop
            ))
            print('{} exploitable pages found'.format(npages))
            if prop != 0:
                mna = 1 / prop
                print('Minimum (contiguous) memory required: {} KiB'.format(math.ceil(mna) * 8))
                print('Mean number of attacks until successful: {:.1f}'.format(mna))
                print('Mean time to successful attack: {:.1f} seconds (assuming {:.1f}ms/attack)'.format(mna * self.atk_time * 10**-3, self.atk_time))



class FliptableEstimator(BaseEstimator):

    def __init__(self, fliptbl, memsys, h_time):
        self.fliptbl = fliptbl
        self.msys = memsys
        super().__init__()
        self.atk_time = self.compute_atk_time(h_time) 

    def iter_attacks(self):
        for atk in self.fliptbl:
            yield _map_attack(atk, self.msys)
    
    def compute_atk_time(self, h_time):
        n_tgts = len(self.fliptbl.attacks[0].targets)
        s_time = scan_time * n_tgts/2 * 3
        f_time = fill_time * n_tgts
        return (f_time + s_time + h_time) / 10**6 # results in ms 


    @classmethod
    def main(cls, profile_file, msys_file, model, h_time):
        """Set up an estimator, run an exploit and print out statistics"""
        ftbl = fliptable.Fliptable.load_file(profile_file)
        msys = dramtrans.MemorySystem()
        msys.load_file(msys_file)
        est = cls(ftbl, msys, h_time)
        est.run_exploit(model)
        est.print_stats()
