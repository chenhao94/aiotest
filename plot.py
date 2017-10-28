#!/usr/bin/python
import sys
import os
import re
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

workload = ['4K-Discrete', '4K-Overlay', '64K-Discrete', '64K-Overlay', '1M-Discrete', '1M-Overlay']
files = ['Single', 'Multi']
lib = ['Posix', 'Fstream', 'PosixAIO']
color = ['r', 'g', 'b']
rw = ['r','w','rw']

working_dir = os.getcwd() + "/" + sys.argv[1]
width = .2
fig, axarr = plt.subplots(6, 2, figsize=(50,50))

for wid, w in enumerate(workload):
    for fid, f in enumerate(files):
        x = []
        y = {}
        for l in lib:
            y[l] = []
        y['locked_time'] = []
        y['avg_iotime'] = []
        y['max_iotime'] = []
        for t in xrange(1,5):
            for r in rw:
                x.append(r+str(t))
                for l in lib:
                    filepath = "%s/%s/%s/%s/%s/%d.log" % (working_dir, w, f, l, r, t)
                    fi = open(filepath, "r")
                    res = fi.readlines()[-1]
                    fi.close()
                    m = re.search("(?P<time>\w+\.\w+) s in total, (?P<lock>\w+\.\w+) s holding lock, (?P<avgio>\w+\.\w+) s avg true io time, (?P<maxio>\w+\.\w+) s max true io time.*?(?P<iops>\w+\.\w+) iops$", res)
                    if m is None:
                        m = re.search("(?P<time>\w+\.\w+) s in total, .*?(?P<iops>\w+\.\w+) iops$", res)
                        lock = 0
                        avgio = 0
                        maxio = 0
                    else:
                        lock = float(m.group('lock'))
                        avgio = float(m.group('avgio'))
                        maxio = float(m.group('maxio'))
                    y[l].append(float(m.group('iops')) / 1000.)
                    if l == 'PosixAIO':
                       y['locked_time'].append(lock / float(m.group('time'))) 
                       y['avg_iotime'].append(avgio / float(m.group('time'))) 
                       y['max_iotime'].append(maxio / float(m.group('time'))) 
                    
        ax1 = axarr[wid, fid]
        ax1.set_title("%s - %s File" % (w, f))
        print "%s - %s File" % (w, f)
        xpos = [1.5 * width + j for j in xrange(0, len(y[l]))]
        ax1.set_xticks(xpos)
        ax1.set_xticklabels(tuple(x))
        ax1.set_ylabel("kiops")
        legend = []
        for i, l in enumerate(lib):
            legend.append(ax1.bar([i * width + j for j in xrange(0, len(y[l]))], y[l], width, color=color[i])[0])
        ax2 = ax1.twinx()
        ax2.set_ylim(0., 1.)
        p = ax2.plot(xpos, y['locked_time'], 'kx-', label = 'locked time ratio')
        p += ax2.plot(xpos, y['avg_iotime'], 'mo-', label = 'avg io time ratio')
        p += ax2.plot(xpos, y['max_iotime'], 'c*-', label = 'max io time ratio')
        ax2.legend(legend + p, tuple(lib + ['locked ratio', 'avg io ratio', 'max io ratio']), loc='upper center', ncol=2, bbox_to_anchor=(0.2, 1.10))
plt.savefig("%s/res.png" % (working_dir))
