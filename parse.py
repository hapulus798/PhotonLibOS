import os
import numpy as np

rootdir = "./perftest2_write"
fnames = os.listdir(rootdir)
for fname in fnames:
    path = "{}/{}".format(rootdir, fname)
    iodepth = fname.split(".")[0].split("_")[0]
    bs = fname.split(".")[0].split("_")[1]
    with open(path, "r") as f:
        lines = f.readlines()[-11:]
        bws = [float(line.strip().split(" ")[3]) for line in lines]
        bw = np.mean(bws)
        print("{},{},{}".format(iodepth, bs, bw))