import os
import numpy as np

rootdir = "./tmp/spdk-read"
fnames = os.listdir(rootdir)
for fname in fnames:
    path = "{}/{}".format(rootdir, fname)
    iodepth = fname.split(".")[0].split("_")[0]
    bs = fname.split(".")[0].split("_")[1]
    with open(path, "r") as f:
        lines = f.readlines()[-9:]
        # lines = f.readlines()[-10:-1]
        bws = [float(line.strip().split(" ")[3]) for line in lines]
        # bws = [float(line.strip().split(" ")[-2]) for line in lines]
        bw = np.mean(bws)
        print("{},{},{}".format(iodepth, bs, bw))