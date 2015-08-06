#!/usr/bin/python -i

"""Module with predefined plotting and targets for UserTreeDSMC::CollideIon output

The main functions begin with make* function.  After make* (or a
readDB() which is called by a make*), you may use showlabs() to see
the name tags for all available fields.

   setTags(list)           : set list of run tags (default: "run")

   readDB()                : read OUTLOG files and build database

   showLabs()              : show the fields available for plotting

   makeM(xl, labs)         : plot fields in list "labs" against field "xl"

   makeP(xl, lab)          : plot field "lab" against field "xl"

"""

import matplotlib.pyplot as plt
import numpy as np
import os, sys

tags     = ["run"]
flab     = []

H_spc    = ['(1,1)', '(1,2)']
He_spc   = ['(2,1)', '(2,2)', '(2,3)']
temps    = ['Tion(1)','Tion(2)','Telc(1)','Telc(2)']
energies = ['Eion(1)','Eion(2)','Eelc(1)','Eelc(2)']
E_sum    = ['Ions_E', 'Elec_E']

def setTags(intags):
    """ Set desired OUTLOG file tags for plotting"""
    tags = intags


def readDB():
    global flab

    db   = {}
    flab = []

    for tag in tags:
        file = open(tag + ".species")
        data = []
        line = file.readline()  # Label line
        labs = [v for v in line[1:].split()]
        for line in file:
            if line.find('#')<0:
                data.append([float(v) for v in line.split()])

        a = np.array(data).transpose()

        db[tag] = {}

        for i in range(a.shape[0]):
            db[tag][labs[i]] = a[i]

        for v in labs:
            if v not in flab: flab.append(v)

    return db


def showLabs():
    icnt = 0
    for v in flab:
        icnt += 1
        print "{:10s}".format(v),
        if icnt % 6 == 0: print


def makeM(xl, labs):
    db = readDB()

    for lab in labs+[xl]:
        if lab not in flab:
            print "No such field, available data is:"
            showLabs()
            return

    for t in tags:
        for lab in labs:
            l = t + ":" + lab
            plt.plot(db[t][xl], db[t][lab], '-', label=l)
    plt.legend()
    plt.xlabel(xl)
    plt.ylabel(lab)
    plt.show()

def makeP(xl, lab):
    db = readDB()

    for l in [xl, lab]:
        if l not in flab:
            print "No such field, available data is:"
            showLabs()
            return
    
    for t in tags:
        plt.plot(db[t][xl], db[t][lab], '-', label=t)
    plt.legend()
    plt.xlabel(xl)
    plt.ylabel(lab)
    plt.show()