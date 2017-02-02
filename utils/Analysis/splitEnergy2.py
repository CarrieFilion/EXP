#!/usr/bin/python

# -*- Python -*-
# -*- coding: utf-8 -*-

import sys, os, argparse
import numpy as np
import matplotlib.pyplot as plt
import getSpecies as gs

parser = argparse.ArgumentParser(description='Read DSMC species file and plot energies for each species')
parser.add_argument('-t', '--tscale', default=1000.0,  type=float, help='System time units in years')
parser.add_argument('-T', '--Tmax', default=1000000.0, type=float, help='Maximum time in years')
parser.add_argument('-l', '--log', default=False, action="store_true", help='Logarithmic energy scale')
parser.add_argument('tags', nargs='*', help='Files to process')

args = parser.parse_args()

labs = args.tags
        
fields = [ [ ['Eion(1)', 'Sion(1)'], ['Eelc(1)', 'Selc(1)'] ],
           [ ['Eion(2)', 'Sion(2)'], ['Eelc(2)', 'Selc(2)'] ] ]
labels = [ 'Hydrogen', 'Helium' ]

d = {}
for v in labs:
	d[v] = gs.readDB(v)

f, ax = plt.subplots(2, 2, sharex='col')
for x in [item for sublist in ax for item in sublist]:
        box = x.get_position()
        newBox = [box.x0+0.05*box.width, box.y0, 0.85*box.width, box.height]
        x.set_position(newBox)

for n in range(2):
        for m in range(2):
                for v in labs:
                        for f in fields[n][m]:
                                if f in d[v]:
                                        indx = np.searchsorted(d[v]['Time'], args.Tmax/args.tscale)
                                if args.log:
                                        ax[n][m].semilogy(d[v]['Time'][0:indx]*args.tscale, d[v][f][0:indx], '-', label=v+':'+f)
                                else:
                                        ax[n][m].plot(d[v]['Time'][0:indx]*args.tscale, d[v][f][0:indx], '-', label=v+':'+f)

                ax[n][m].legend(prop={'size':8}, bbox_to_anchor=(1.02, 1), loc=2, borderaxespad=0.0)
                if n==len(labels)-1: ax[n][m].set_xlabel('Time')
                if m==0: ax[n][m].set_ylabel('Energy')
                ax[n][m].set_title(labels[n])
plt.show()