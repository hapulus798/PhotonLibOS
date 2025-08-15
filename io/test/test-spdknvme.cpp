#include <photon/photon.h>
#include <photon/thread/thread.h>
#include <photon/thread/thread11.h>
#include <photon/thread/workerpool.h>
#include <photon/common/iovector.h>
#include <photon/io/spdknvme-wrapper.h>
#include <chrono>
#include <csignal>
#include <gflags/gflags.h>
#include "../../test/gtest.h"

class SPDKNVMe {
public:
    void init() {
        ASSERT_EQ(photon::spdk::nvme_env_init(), 0);
        ctrlr = photon::spdk::nvme_probe_attach(trid_str);
        ASSERT_NE(ctrlr, nullptr);
        ns = photon::spdk::nvme_get_namespace(ctrlr, nsid);
        ASSERT_NE(ns, nullptr);
        photon::PhotonOptions opt;
        opt.use_pooled_stack_allocator = true;
        opt.bypass_threadpool = true;
        ASSERT_EQ(photon::init(photon::INIT_EVENT_DEFAULT, photon::INIT_IO_DEFAULT, opt), 0);
    }

    void fini() {
        GTEST_LOG_(INFO) << "SPDK NVMe fini";
        photon::fini();
        GTEST_LOG_(INFO) << "after photon::fini";
        photon::spdk::nvme_detach(ctrlr);
        GTEST_LOG_(INFO) << "after detach";
        photon::spdk::nvme_env_fini();
        GTEST_LOG_(INFO) << "after nvme_env_fini";
    }

    static const char* trid_str;
    static const int nsid;
    struct spdk_nvme_ctrlr* ctrlr;
    struct spdk_nvme_ns* ns;
};

const char* SPDKNVMe::trid_str = "trtype:pcie traddr:0000:86:00.0";
const int SPDKNVMe::nsid = 1;

class SPDKNVMeTestEnv : public ::testing::Environment {
public:
    void SetUp() override {
        ASSERT_EQ(nvme_info, nullptr);
        nvme_info = new SPDKNVMe();
        nvme_info->init();
        GTEST_LOG_(INFO) << "SetUp Success";
    }

    void TearDown() override {
        ASSERT_NE(nvme_info, nullptr);
        nvme_info->fini();
        delete nvme_info;
        GTEST_LOG_(INFO) << "TearDown Success";

    }

    static SPDKNVMe* nvme_info;
};

SPDKNVMe* SPDKNVMeTestEnv::nvme_info = nullptr;

class SPDKNVMeTest : public ::testing::Test {
public:
    SPDKNVMe* nvme_info = SPDKNVMeTestEnv::nvme_info;
};

TEST_F(SPDKNVMeTest, rw) {
    struct spdk_nvme_ctrlr* ctrlr = nvme_info->ctrlr;
    struct spdk_nvme_ns* ns = nvme_info->ns;

    struct spdk_nvme_qpair* qpair = photon::spdk::nvme_ctrlr_alloc_io_qpair(ctrlr, nullptr, 0);
    EXPECT_NE(qpair, nullptr);
    DEFER(photon::spdk::nvme_ctrlr_free_io_qpair(ctrlr, qpair));

    uint32_t sectorsz = spdk_nvme_ns_get_sector_size(ns);
    uint32_t nsec = 8;
    uint64_t bufsz = sectorsz * nsec;

    void* buf_write = spdk_zmalloc(bufsz, 0, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    void* buf_read = spdk_zmalloc(bufsz, 0, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    EXPECT_NE(buf_write, nullptr);
    EXPECT_NE(buf_read, nullptr);
    DEFER(spdk_free(buf_write));
    DEFER(spdk_free(buf_read));

    // prepare datas to write
    for (int i=0; i<nsec; i++) {
        memset((char*)buf_write + i * sectorsz, i, sectorsz);
    }

    std::vector<photon::join_handle*> jhs_write;
    for (int i=0; i<nsec; i++) {
        char* buf = (char*)buf_write + i * sectorsz;
        jhs_write.emplace_back(photon::thread_enable_join(photon::thread_create11([](struct spdk_nvme_ns* ns, spdk_nvme_qpair* qpair, void* buf, uint64_t lba, uint32_t lba_count){
            EXPECT_EQ(photon::spdk::nvme_ns_cmd_write(ns, qpair, buf, lba, lba_count, 0), 0);
        }, ns, qpair, buf, i, 1)));
    }
    for (auto jh: jhs_write) {
        photon::thread_join(jh);
    }

    std::vector<photon::join_handle*> jhs_read;
    for (int i=0; i<nsec; i++) {
        char* buf = (char*)buf_read + i * sectorsz;
        jhs_read.emplace_back(photon::thread_enable_join(photon::thread_create11([](struct spdk_nvme_ns* ns, spdk_nvme_qpair* qpair, void* buf, uint64_t lba, uint32_t lba_count){
            EXPECT_EQ(photon::spdk::nvme_ns_cmd_read(ns, qpair, buf, lba, lba_count, 0), 0);
        }, ns, qpair, buf, i, 1)));
    }
    for (auto jh: jhs_read) {
        photon::thread_join(jh);
    }

    // checking
    EXPECT_EQ(memcmp(buf_write, buf_read, bufsz), 0);
}

TEST_F(SPDKNVMeTest, rwv) {
    struct spdk_nvme_ctrlr* ctrlr = nvme_info->ctrlr;
    struct spdk_nvme_ns* ns = nvme_info->ns;

    struct spdk_nvme_qpair* qpair = photon::spdk::nvme_ctrlr_alloc_io_qpair(ctrlr, nullptr, 0);
    EXPECT_NE(qpair, nullptr);
    DEFER(photon::spdk::nvme_ctrlr_free_io_qpair(ctrlr, qpair));

    uint32_t sectorsz = spdk_nvme_ns_get_sector_size(ns);
    uint32_t nsec = 4;
    uint64_t bufsz = sectorsz * nsec;

    void* buf_write = spdk_zmalloc(bufsz, 0, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    void* buf_read = spdk_zmalloc(bufsz, 0, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    EXPECT_NE(buf_write, nullptr);
    EXPECT_NE(buf_read, nullptr);
    DEFER(spdk_free(buf_write));
    DEFER(spdk_free(buf_read));

    // prepare datas to write
    char* buf_write_a = (char*)buf_write;
    char* buf_write_b = (char*)buf_write + sectorsz;
    char* buf_write_c = (char*)buf_write + 3 * sectorsz;
    memset(buf_write_a, 'a', sectorsz);
    memset(buf_write_b, 'b', 2 * sectorsz);
    memset(buf_write_c, 'c', sectorsz);

    IOVector iovs_write;
    iovs_write.push_back(buf_write_a, sectorsz);
    iovs_write.push_back(buf_write_b, 2 * sectorsz);
    iovs_write.push_back(buf_write_c, sectorsz);

    IOVector iovs_read;
    iovs_read.push_back(buf_read, sectorsz);
    iovs_read.push_back((char*)buf_read + sectorsz, sectorsz);
    iovs_read.push_back((char*)buf_read + 2 * sectorsz, 2 * sectorsz);

    GTEST_LOG_(INFO) << "writev";
    EXPECT_EQ(photon::spdk::nvme_ns_cmd_writev(ns, qpair, iovs_write.iovec(), iovs_write.iovcnt(), nsec, nsec, 0), 0);
    GTEST_LOG_(INFO) << "readv";
    EXPECT_EQ(photon::spdk::nvme_ns_cmd_readv(ns, qpair, iovs_read.iovec(), iovs_read.iovcnt(), nsec, nsec, 0), 0);

    // checking
    EXPECT_EQ(memcmp(buf_write, buf_read, bufsz), 0);
}

TEST_F(SPDKNVMeTest, multi_thread) {
    struct spdk_nvme_ctrlr* ctrlr = nvme_info->ctrlr;
    struct spdk_nvme_ns* ns = nvme_info->ns;

    int nvcpu = 1;
    int mode = 1;
    photon::WorkPool wp(nvcpu, 0, 0, mode);

    // int ntest = 2097152;
    int ntest = 1024;
    uint32_t sectorsz = spdk_nvme_ns_get_sector_size(ns);

    struct spdk_nvme_io_qpair_opts opts;
    spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr, &opts, sizeof(opts));
    printf("default io_queue_requests=%u\n", opts.io_queue_requests);   // SQ大小
    printf("default io_queue_size=%u\n", opts.io_queue_size);           // CQ大小
    opts.io_queue_requests = 1024;

    // writes
    GTEST_LOG_(INFO) << "writes";
    photon::semaphore sem;
    for (int i=0; i<ntest; i++) {
        wp.thread_migrate(photon::thread_create11([&](int idx){
            // GTEST_LOG_(INFO) << "write " << idx;
            struct spdk_nvme_qpair* qpair = photon::spdk::nvme_ctrlr_alloc_io_qpair(ctrlr, &opts, sizeof(opts));
            EXPECT_NE(qpair, nullptr);
            DEFER(photon::spdk::nvme_ctrlr_free_io_qpair(ctrlr, qpair));

            void* buffer = spdk_zmalloc(sectorsz, 0, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
            EXPECT_NE(buffer, nullptr);
            DEFER(spdk_free(buffer));

            memset(buffer, idx, sectorsz);
            // GTEST_LOG_(INFO) << "write before " << idx;
            EXPECT_EQ(photon::spdk::nvme_ns_cmd_write(ns, qpair, buffer, idx, 1, 0), 0);
            // GTEST_LOG_(INFO) << "write after " << idx;
            sem.signal(1);
        }, i));
    }
    sem.wait(ntest);

    // read
    GTEST_LOG_(INFO) << "read";
    struct spdk_nvme_qpair* qpair = photon::spdk::nvme_ctrlr_alloc_io_qpair(ctrlr, nullptr, 0);
    EXPECT_NE(qpair, nullptr);
    DEFER(photon::spdk::nvme_ctrlr_free_io_qpair(ctrlr, qpair));

    void* buffer = spdk_zmalloc(sectorsz * ntest, 0, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    EXPECT_NE(buffer, nullptr);
    DEFER(spdk_free(buffer));
    EXPECT_EQ(photon::spdk::nvme_ns_cmd_read(ns, qpair, buffer, 0, ntest, 0), 0);

    // checking
    void* checkbuf = malloc(sectorsz * ntest);
    EXPECT_NE(checkbuf, nullptr);
    DEFER(free(checkbuf));
    for (int i=0; i<ntest; i++) {
        memset((char*)checkbuf + i * sectorsz, i, sectorsz);
    }
    EXPECT_EQ(memcmp(buffer, checkbuf, sectorsz * ntest), 0);
    GTEST_LOG_(INFO) << "read done";
}



using TimePoint = std::chrono::high_resolution_clock::time_point;

// class IOHelper {
// public:
//     IOHelper(struct spdk_nvme_ctrlr* ctrlr, struct spdk_nvme_ns* ns) : ctrlr_(ctrlr), ns_(ns) {
//         assert(ctrlr_ != nullptr && ns_ != nullptr);
//         block_size_ = spdk_nvme_ns_get_sector_size(ns_);
//         assert(block_size_ > 0);
//         spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr_, &opts, sizeof(opts));
//         opts.io_queue_requests = 4096;
//     }

//     void IOFunc(struct spdk_nvme_qpair* qpair, void* buf, off_t block_offset, size_t block_count, bool iswrite) {
//         int rc = 0;
//         if (iswrite) rc = photon::spdk::nvme_ns_cmd_write(ns_, qpair, buf, block_offset, block_count, 0);
//         else rc = photon::spdk::nvme_ns_cmd_read(ns_, qpair, buf, block_offset, block_count, 0);
//         if (rc != 0) {
//             printf("IOFunc, rc = %d, block_offset=%lu\n", rc, block_offset);
//             assert(rc == 0);
//         }
//     }

//     void* GetIOBuffer(uint64_t nblocks, bool iswrite) {
//         uint64_t nbytes = nblocks * block_size_;
//         void* buf = spdk_zmalloc(nbytes, block_size_, nullptr, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
//         if (iswrite) memset(buf, 0x5F, nbytes);
//         return buf;
//     }

//     void FreeIOBuffer(void* buf) { spdk_free(buf); }

//     uint64_t GetBlockSize() const { return block_size_;}

//     struct spdk_nvme_qpair* GetQPair() {
//         return photon::spdk::nvme_ctrlr_alloc_io_qpair(ctrlr_, &opts, sizeof(opts));
//     }

//     void FreeQPair(struct spdk_nvme_qpair* qpair) {
//         photon::spdk::nvme_ctrlr_free_io_qpair(ctrlr_, qpair);
//     }

// private:
//     struct spdk_nvme_ctrlr *ctrlr_ = nullptr;
//     struct spdk_nvme_ns *ns_ = nullptr;
//     uint64_t block_size_ = 0;
//     struct spdk_nvme_io_qpair_opts opts;
// };

// TEST_F(SPDKNVMeTest, performance) {
//     uint64_t nblocks_per_req = 8;
//     uint64_t batch_size = 1;
//     uint64_t total_blocks = 2097152 * batch_size;
//     bool iswrite = false;

//     IOHelper io_helper(nvme_info->ctrlr, nvme_info->ns);
//     void* buf = io_helper.GetIOBuffer(nblocks_per_req * batch_size, iswrite);
//     uint64_t block_size = io_helper.GetBlockSize();
//     uint64_t start_block = 0;
//     uint64_t remain = total_blocks;

//     auto qpair = io_helper.GetQPair();

//     TimePoint start_time = std::chrono::high_resolution_clock::now();
//     while (remain > 0) {
//         uint64_t thisone = std::min(remain, nblocks_per_req * batch_size);
//         io_helper.IOFunc(qpair, buf, start_block, thisone, iswrite);
//         remain -= thisone;
//         start_block += thisone;
//     }
//     TimePoint end_time = std::chrono::high_resolution_clock::now();

//     io_helper.FreeQPair(qpair);
//     io_helper.FreeIOBuffer(buf);

//     double cost = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count() / 1000.0; // dura unit is us
//     double thp = (total_blocks * block_size / 1024.0 / 1024.0) / (cost / 1e6);
//     printf("thp: %.4f MiB/s, cost: %.4f us\n", thp, cost);
// }

// TEST_F(SPDKNVMeTest, performance) {
//     auto ctrlr = nvme_info->ctrlr;
//     auto ns = nvme_info->ns;
//     struct spdk_nvme_io_qpair_opts opts;
//     spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr, &opts, sizeof(opts));
//     opts.delay_cmd_submit = true;
//     opts.io_queue_requests = 4096;
//     opts.io_queue_size = 4096;
//     auto qpair = photon::spdk::nvme_ctrlr_alloc_io_qpair(ctrlr, &opts, sizeof(opts));
// }


DEFINE_uint64(bs, 4096, "block size in bytes");
DEFINE_uint64(iodepth, 128, "num of requests on the fly at the same time");
DEFINE_uint32(size, 1, "total access data, unit is GiB");
DEFINE_bool(iswrite, false, "randread or randwrite");

static std::atomic<uint64_t> qps{0};
#define ROUND_DOWN(N, S) ((N) & ~((S) - 1))

TEST_F(SPDKNVMeTest, performance2) {
    // std::signal(SIGINT, [](int signal){
    //     if (signal == SIGINT) {
    //         auto f = fopen(std::string("nreap_stats_"+std::to_string(FLAGS_iodepth)+"_"+std::to_string(FLAGS_bs)+".txt").c_str(), "w+");
    //         for (auto& x: photon::spdk::nreap_stats) fprintf(f, "%d,", x);
    //         fprintf(f, "\n");
    //         fclose(f);
    //     }
    //     std::exit(0);
    // });

    const uint64_t BLOCKSIZE = FLAGS_bs;
    const uint64_t LBACOUNT = BLOCKSIZE / 512;
    const uint64_t IODEPTH = FLAGS_iodepth;
    const uint32_t TOTALGB = FLAGS_size;
    const bool ISWRITE = FLAGS_iswrite;

    GTEST_LOG_(INFO) << "config: size=" << TOTALGB << "GiB, bs=" << BLOCKSIZE << "(i.e. " << BLOCKSIZE/1024 << "k, " << LBACOUNT << " sectors), iodepth=" << IODEPTH;
    GTEST_LOG_(INFO) << "config: iswrite=" << ISWRITE;

    auto ctrlr = nvme_info->ctrlr;
    auto ns = nvme_info->ns;
    struct spdk_nvme_io_qpair_opts opts;
    spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr, &opts, sizeof(opts));
    opts.delay_cmd_submit = true;
    opts.io_queue_requests = 4096;
    opts.io_queue_size = 4096;
    auto qpair = photon::spdk::nvme_ctrlr_alloc_io_qpair(ctrlr, &opts, sizeof(opts));

    auto task_read = [LBACOUNT, BLOCKSIZE, TOTALGB](struct spdk_nvme_ns* ns, struct spdk_nvme_qpair* qpair){
        auto random = [](uint64_t N) -> uint64_t {
            static std::random_device rd;
            static std::mt19937_64 gen(rd());
            return gen() % N;
        };
        uint64_t max_offset = 1024 * 1024 * 1024UL * TOTALGB / 512 - LBACOUNT;  // test range is 1GiB (same to fio's size=1g), should write this range first, or the thp is very high(guess directly return when meet zero)
        void* buf = spdk_dma_zmalloc(BLOCKSIZE, 4096, nullptr);
        uint64_t offset;
        while (true) {
            offset = random(max_offset);
            EXPECT_EQ(0, photon::spdk::nvme_ns_cmd_read(ns, qpair, buf, offset, LBACOUNT, 0));
            qps.fetch_add(1, std::memory_order_relaxed);
        };
    };

    auto task_write = [LBACOUNT, BLOCKSIZE, TOTALGB](struct spdk_nvme_ns* ns, struct spdk_nvme_qpair* qpair){
        auto random = [](uint64_t N) -> uint64_t {
            static std::random_device rd;
            static std::mt19937_64 gen(rd());
            return gen() % N;
        };
        uint64_t max_offset = 1024 * 1024 * 1024UL * TOTALGB / 512 - LBACOUNT;
        void* buf = spdk_dma_zmalloc(BLOCKSIZE, 4096, nullptr);
        memset(buf, 0x5F, BLOCKSIZE);
        uint64_t offset;
        while (true) {
            offset = random(max_offset);
            EXPECT_EQ(0, photon::spdk::nvme_ns_cmd_write(ns, qpair, buf, offset, LBACOUNT, 0));
            qps.fetch_add(1, std::memory_order_relaxed);
        };
    };

    auto show_qps_loop = [BLOCKSIZE]{
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cerr << "QPS: " << qps.load() << ", BW: " << qps.load() * BLOCKSIZE / 1024.0 / 1024.0 << " MiB/s" << std::endl;
            qps.store(0, std::memory_order_relaxed);
        }
    };

    new std::thread(show_qps_loop);

    if (ISWRITE) {
        for (int i=0; i<IODEPTH; i++) {
            photon::thread_create11(task_write, ns, qpair);
        }
    }
    else {
        for (int i=0; i<IODEPTH; i++) {
            photon::thread_create11(task_read, ns, qpair);
        }
    }


    photon::thread_sleep(-1);
}

TEST_F(SPDKNVMeTest, seqwrite) {
    const uint64_t BLOCKSIZE = FLAGS_bs;
    const uint64_t LBACOUNT = BLOCKSIZE / 512;
    const uint64_t IODEPTH = FLAGS_iodepth;
    const uint32_t TOTALGB = FLAGS_size;

    GTEST_LOG_(INFO) << "config: size=" << TOTALGB << "GiB, bs=" << BLOCKSIZE << "(i.e. " << BLOCKSIZE/1024 << "k, " << LBACOUNT << " sectors), iodepth=" << IODEPTH;
    GTEST_LOG_(INFO) << "config: seqwrite raw device, prepare for read";

    auto ctrlr = nvme_info->ctrlr;
    auto ns = nvme_info->ns;
    struct spdk_nvme_io_qpair_opts opts;
    spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr, &opts, sizeof(opts));
    opts.delay_cmd_submit = true;
    opts.io_queue_requests = 4096;
    opts.io_queue_size = 4096;
    auto qpair = photon::spdk::nvme_ctrlr_alloc_io_qpair(ctrlr, &opts, sizeof(opts));

    auto task = [LBACOUNT, BLOCKSIZE](struct spdk_nvme_ns* ns, struct spdk_nvme_qpair* qpair, int idx, uint64_t begin_offset, uint64_t nblocks){
        void* buf = spdk_dma_zmalloc(BLOCKSIZE, 4096, nullptr);
        memset(buf, 0x5F, BLOCKSIZE);
        uint64_t offset = begin_offset;
        uint64_t remain = nblocks;
        while (remain > 0) {
            EXPECT_EQ(0, photon::spdk::nvme_ns_cmd_write(ns, qpair, buf, offset, LBACOUNT, 0));
            offset += LBACOUNT;
            remain -= LBACOUNT;
            qps.fetch_add(1, std::memory_order_relaxed);
        }
        std::cerr << "complete: " << idx << std::endl;
    };

    auto show_qps_loop = [BLOCKSIZE]{
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cerr << "QPS: " << qps.load() << ", BW: " << qps.load() * BLOCKSIZE / 1024.0 / 1024.0 << " MiB/s" << std::endl;
            qps.store(0, std::memory_order_relaxed);
        }
    };

    new std::thread(show_qps_loop);

    uint64_t max_offset = 1024 * 1024 * 1024UL * TOTALGB / 512;
    uint64_t eachone = max_offset / IODEPTH;
    uint64_t begin_offset = 0;

    for (int i=0; i<IODEPTH; i++) {
        photon::thread_create11(task, ns, qpair, i, begin_offset, eachone);
        begin_offset += eachone;
    }

    photon::thread_sleep(-1);
}


int main(int argc, char** argv) {
    testing::AddGlobalTestEnvironment(new SPDKNVMeTestEnv);
    testing::InitGoogleTest(&argc, argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}