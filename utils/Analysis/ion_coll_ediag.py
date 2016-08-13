#!/usr/bin/python

# -*- Python -*-
# -*- coding: utf-8 -*-

"""
Program to display the particle collision counts for each type
using diagnostic output from the CollideIon class in the UserTreeDSMC
module.

There are two simple routines here.  The main routine that parses the
input command line and a plotting/parsing routine.

Examples:

	$ python ion_coll_energy.py -m 10 run2

Plots the collision count types for each ion type.

"""

import sys, getopt
import copy
import string
import numpy as np
import matplotlib.pyplot as pl
import scipy.interpolate as ip


def plot_data(filename, eloss, msz, logY, dot):
    """Parse and plot the *.ION_coll output files generated by
    CollideIon

    Parameters:

    filename (string): is the input datafile name

    msz (numeric): marker size

    logY(bool): use logarithmic y axis

    dot (bool): if True, markers set to dots

    """

    # Marker type
    #
    if dot: mk = '.'
    else:   mk = '*'

    # Translation table to convert vertical bars and comments to spaces
    #
    trans = string.maketrans("#|", "  ")

    # Initialize data and header containers
    #
    tabl  = {}
    time  = []
    temp  = []
    etot  = []
    erat  = []
    edsp  = []
    ekeE  = []
    ekeI  = []
    potI  = []
    elsC  = []
    elos  = []
    ncol  = 16
    lead  = 2
    tail  = 12
    data  = {}

    # Species
    #
    spec  = ['H', 'H+', 'He', 'He+', 'He++']
    for v in spec: data[v] = {}

    # Read and parse the file
    #
    file  = open(filename + ".ION_coll")
    for line in file:
        if line.find('Time')>=0:    # Get the labels
            next = True
            labels = line.translate(trans).split()
        if line.find('[1]')>=0:     # Get the column indices
            toks = line.translate(trans).split()
            for i in range(lead, len(toks)-tail):
                j = int(toks[i][1:-1]) - 1
                tabl[labels[j]] = i
                idx = (i-lead) / ncol
                data[spec[idx]][labels[j]] = []
        if line.find('#')<0:        # Read the data lines
            toks = line.translate(trans).split()
            allZ = True             # Skip lines with zeros only
            for i in range(lead,len(toks)):
                if float(toks[i])>0.0: 
                    allZ = False
                    break
            if not allZ:            
                # A non-zero line . . .  Make sure field counts are the
                # same (i.e. guard against the occasional badly written
                # output file
                if len(toks) == len(labels):
                    time.append(float(toks[0]))
                    temp.append(float(toks[1]))
                    etot.append(float(toks[-1]))
                    erat.append(float(toks[-2]))
                    edsp.append(float(toks[-3]))
                    potI.append(float(toks[-4]))
                    ekeE.append(float(toks[-5]))
                    ekeI.append(float(toks[-6]))
                    elsC.append(float(toks[-7]))
                    elos.append(float(toks[-8]))
                    for i in range(lead,len(toks)-tail):
                        idx = (i-tail) / ncol
                        data[spec[idx]][labels[i]].append(float(toks[i]))
                else:
                    print "toks=", len(toks), " labels=", len(labels)

    time_1 = []
    temp_1 = []
    temp_2 = []
    file  = open(filename + ".species")
    for line in file:
        if line.find('#')<0:
            toks = line.split()
            time_1.append(float(toks[0]))
            temp_1.append(float(toks[1]))
            temp_2.append(float(toks[10]))

    # Fields to plot
    #
    pl.subplot(2,2,1)
    pl.xlabel('Time')
    pl.ylabel('Temp')
    pl.plot(time_1, temp_1, '-', label="ion")
    pl.plot(time_1, temp_2, '-', label="elec")
    pl.legend(prop={'size':10}).draggable()
    #
    pl.subplot(2,2,2)
    pl.xlabel('Time')
    pl.ylabel('Ratio')
    pl.plot(time, erat, '-')
    #
    ax = pl.subplot(2,2,3)
    # ax.plot(time, etot, '-', label="total")
    ax.plot(time, ekeI, '-', label="ion")
    ax.plot(time, ekeE, '-', label="electron")
    ax.plot(time, potI, '-', label="ion pot")
    ax.plot(time, edsp, '-', label="e-disp")
    # Shrink current axis by 20%
    # box = ax.get_position()
    # ax.set_position([box.x0, box.y0, box.width * 0.8, box.height])
    # Put a legend to the right of the current axis
    # ax.legend(loc='center left', bbox_to_anchor=(1, 0.5))
    ax.legend(prop={'size':10}).draggable()
    # Labels
    ax.set_xlabel('Time')
    ax.set_ylabel('Energy')
    #
    pl.subplot(2,2,4)
    pl.xlabel('Time')
    pl.ylabel('Energy')
    if eloss:
        esKE = np.add(ekeI, ekeE)
        etot = np.add(etot, potI)
        if logY:
            pl.semilogy(time, etot, '-', label="E total")
            pl.semilogy(time, elos, '-', label="E lost (D)")
            pl.semilogy(time, elsC, '-', label="E lost (C)")
            pl.semilogy(time, potI, '-', label="Pot")
            pl.semilogy(time, esKE, '-', label="KE")
        else:
            pl.plot(time, etot, '-', label="E total")
            pl.plot(time, elos, '-', label="E lost (D)")
            pl.plot(time, elsC, '-', label="E lost (C)")
            pl.plot(time, potI, '-', label="Pot")
            pl.plot(time, esKE, '-', label="KE")
        pl.legend(prop={'size':10}).draggable()
    else:
        pl.plot(time, etot, '-')
    #
    pl.show()


def main(argv):
    """ Parse the command line and call the parsing and plotting routine """

    logY  = False
    dot   = False
    eloss = False
    msz   = 4

    info = '[-e | --eloss | -l | --log | -p | --point | -m <size> | --msize=<size>] <runtag>'

    try:
        opts, args = getopt.getopt(argv,"hem:lp", ["eloss", "msize=", "log", "point"])
    except getopt.GetoptError:
        print 'Syntax Error'
        print sys.argv[0], info
        sys.exit(2)
    for opt, arg in opts:
        if opt == '-h':
            print sys.argv[0], info
            sys.exit()
        elif opt in ("-p", "--point"):
            dot = True
        elif opt in ("-l", "--log"):
            logY = True
        elif opt in ("-m", "--msize"):
            msz = int(arg)
        elif opt in ("-e", "--eloss"):
            eloss = True

    if len(args)>0:
        filename = args[0];
    else:
        filename = "run";

    plot_data(filename, eloss, msz, logY, dot)

if __name__ == "__main__":
   main(sys.argv[1:])
