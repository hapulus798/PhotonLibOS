#include <photon/photon.h>
#include <photon/thread/thread.h>
#include <photon/thread/thread11.h>
#include <photon/thread/workerpool.h>
#include <photon/common/iovector.h>
#include <photon/io/spdkbdev-wrapper.h>
#include <csignal>
#include <gflags/gflags.h>
#include "../../test/gtest.h"

class SPDKBDev {
public:
    void init() {
        photon::PhotonOptions opt;
        opt.use_pooled_stack_allocator = true;
        opt.bypass_threadpool = true;
        ASSERT_EQ(photon::init(photon::INIT_EVENT_DEFAULT, photon::INIT_IO_DEFAULT, opt), 0);
        photon::spdk::bdev_env_init(json_cfg_path);
        // photon::spdk::bdev_open_ext("Malloc0", true, &desc);
        photon::spdk::bdev_open_ext("Nvme0n1", true, &desc);
        ASSERT_NE(desc, nullptr);
        ch = photon::spdk::bdev_get_io_channel(desc);
        ASSERT_NE(ch, nullptr);
    }

    void fini() {
        photon::spdk::bdev_put_io_channel(ch);
        photon::spdk::bdev_close(desc);
        photon::spdk::bdev_env_fini();
        photon::fini();
    }

    static const char* json_cfg_path;
    struct spdk_bdev_desc* desc;
    struct spdk_io_channel* ch;
};

// path of bdev config json
const char* SPDKBDev::json_cfg_path = "./examples/spdk/bdev_real.json";

class SPDKBDevTestEnv : public ::testing::Environment {
public:
    void SetUp() override {
        ASSERT_EQ(bdev_info, nullptr);
        bdev_info = new SPDKBDev();
        bdev_info->init();
        GTEST_LOG_(INFO) << "SetUp Success";
    }

    void TearDown() override {
        ASSERT_NE(bdev_info, nullptr);
        bdev_info->fini();
        delete bdev_info;
        GTEST_LOG_(INFO) << "TearDown Success";
    }

    static SPDKBDev* bdev_info;
};

SPDKBDev* SPDKBDevTestEnv::bdev_info = nullptr;

class SPDKBDevTest : public ::testing::Test {
public:
    SPDKBDev* bdev_info = SPDKBDevTestEnv::bdev_info;
};

TEST_F(SPDKBDevTest, rw) {
    struct spdk_bdev_desc* desc = bdev_info->desc;
    struct spdk_io_channel* ch = bdev_info->ch;

    uint64_t bufsz = 4096;
    uint64_t blocksz = 512;
    uint64_t nblocks = bufsz / blocksz;

    void* bufwrite = spdk_zmalloc(bufsz, 4096, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    void* bufread = spdk_zmalloc(bufsz, 4096, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    EXPECT_NE(bufwrite, nullptr);
    EXPECT_NE(bufread, nullptr);
    DEFER(spdk_free(bufwrite));
    DEFER(spdk_free(bufread));

    // prepare datas to write
    char test_data[] = "hello world";
    for (uint64_t i=0; i<nblocks; i++) {
        strncpy((char*)bufwrite + i * blocksz, test_data, 12);
    }

    // writes in parallel
    std::vector<photon::join_handle*> ths_write;
    for (uint64_t i=0; i<nblocks; i++) {
        void* buf = (void*)((char*)bufwrite + i * blocksz);
        uint64_t off = i * blocksz, cnt = blocksz;
        ths_write.emplace_back(
            photon::thread_enable_join(
                photon::thread_create11(
                [](int idx, struct spdk_bdev_desc* desc, struct spdk_io_channel* ch, void* buffer, uint64_t offset, uint64_t nbytes) {
                    int rc = photon::spdk::bdev_write(desc, ch, buffer, offset, nbytes);
                    EXPECT_EQ(rc, 0);
                }, i, desc, ch, buf, off, cnt)
            )
        );
    }
    for (auto th : ths_write) { // wait all writes complete
        photon::thread_join(th);
    }

    // reads in parallel
    std::vector<photon::join_handle*> ths_read;
    for (uint64_t i=0; i<nblocks; i++) {
        void* buf = (void*)((char*)bufread + i * blocksz);
        uint64_t off = i * blocksz, cnt = blocksz;
        ths_read.emplace_back(
            photon::thread_enable_join(
                photon::thread_create11(
                [](int idx, struct spdk_bdev_desc* desc, struct spdk_io_channel* ch, void* buffer, uint64_t offset, uint64_t nbytes) {
                    int rc = photon::spdk::bdev_read(desc, ch, buffer, offset, nbytes);
                    EXPECT_EQ(rc, 0);
                }, i, desc, ch, buf, off, cnt)
            )
        );
    }
    for (auto th: ths_read) {   // wait all reads complete
        photon::thread_join(th);
    }

    // checking
    EXPECT_EQ(memcmp(bufwrite, bufread, bufsz), 0);
}

TEST_F(SPDKBDevTest, rw_blocks) {
    struct spdk_bdev_desc* desc = bdev_info->desc;
    struct spdk_io_channel* ch = bdev_info->ch;

    uint64_t bufsz = 4096;
    uint64_t blocksz = 512;
    uint64_t nblocks = bufsz / blocksz;

    void* bufwrite = spdk_zmalloc(bufsz, 4096, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    void* bufread = spdk_zmalloc(bufsz, 4096, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    EXPECT_NE(bufwrite, nullptr);
    EXPECT_NE(bufread, nullptr);
    DEFER(spdk_free(bufwrite));
    DEFER(spdk_free(bufread));

    std::vector<photon::join_handle*> ths_write;
    for (uint64_t i = 0; i < nblocks; i++) {
        void* bufbegin = (void*)((char*)bufwrite + i * blocksz);
        memset(bufbegin, i, blocksz);
        ths_write.emplace_back(photon::thread_enable_join(
        photon::thread_create11([](struct spdk_bdev_desc* desc, struct spdk_io_channel* ch, void* buf, uint64_t offset_blocks, uint64_t num_blocks){
            photon::spdk::bdev_write_blocks(desc, ch, buf, offset_blocks, num_blocks);
        }, desc, ch, bufbegin, i, 1)));
    }
    for (auto th : ths_write) {
        photon::thread_join(th);
    }

    std::vector<photon::join_handle*> ths_read;
    for (uint64_t i = 0; i < nblocks; i++) {
        void* bufbegin = (void*)((char*)bufread + i * blocksz);
        ths_read.emplace_back(photon::thread_enable_join(
        photon::thread_create11([](struct spdk_bdev_desc* desc, struct spdk_io_channel* ch, void* buf, uint64_t offset_blocks, uint64_t num_blocks){
            photon::spdk::bdev_read_blocks(desc, ch, buf, offset_blocks, num_blocks);
        }, desc, ch, bufbegin, i, 1)));
    }
    for (auto th : ths_read) {
        photon::thread_join(th);
    }

    EXPECT_EQ(memcmp(bufwrite, bufread, bufsz), 0);
}

TEST_F(SPDKBDevTest, rwv) {
    struct spdk_bdev_desc* desc = bdev_info->desc;
    struct spdk_io_channel* ch = bdev_info->ch;

    uint64_t bufsz = 4096;
    uint64_t blocksz = 512;
    uint64_t nblocks = bufsz / blocksz;

    void* bufwrite = spdk_zmalloc(bufsz, 4096, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    void* bufread = spdk_zmalloc(bufsz, 4096, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    EXPECT_NE(bufwrite, nullptr);
    EXPECT_NE(bufread, nullptr);
    DEFER(spdk_free(bufwrite));
    DEFER(spdk_free(bufread));

    photon::WorkPool wp(1, 0, 0, 0);

    IOVector iov0, iov1;
    for (uint64_t i = 0; i < nblocks; i++) {
        void* bufbegin = (void*)((char*)bufwrite + i * blocksz);
        memset(bufbegin, i, blocksz);
        if (i % 2 == 0) iov0.push_back(bufbegin, blocksz);
        else iov1.push_back(bufbegin, blocksz);
    }
    EXPECT_EQ(iov0.sum() + iov1.sum(), bufsz);

    photon::semaphore sem;
    wp.async_call(new auto([&]{
        photon::spdk::bdev_writev(desc, ch, iov0.iovec(), iov0.iovcnt(), 0, iov0.sum());
        sem.signal(1);
    }));
    wp.async_call(new auto([&]{
        photon::spdk::bdev_writev(desc, ch, iov1.iovec(), iov1.iovcnt(), iov0.sum(), iov1.sum());
        sem.signal(1);
    }));
    sem.wait(2);


    IOVector iov2;
    for (uint64_t i = 0; i < nblocks; i+=2) {
        void* bufbegin = (void*)((char*)bufread + i * blocksz);
        iov2.push_back(bufbegin, blocksz);
    }
    for (uint64_t i = 1; i < nblocks; i+=2) {
        void* bufbegin = (void*)((char*)bufread + i * blocksz);
        iov2.push_back(bufbegin, blocksz);
    }
    EXPECT_EQ(iov2.iovcnt(), iov0.iovcnt() + iov1.iovcnt());
    wp.async_call(new auto([&]{
        photon::spdk::bdev_readv(desc, ch, iov2.iovec(), iov2.iovcnt(), 0, iov2.sum());
        sem.signal(1);
    }));
    sem.wait(1);

    EXPECT_EQ(memcmp(bufwrite, bufread, bufsz), 0);
}

TEST_F(SPDKBDevTest, rwv_blocks) {
    struct spdk_bdev_desc* desc = bdev_info->desc;
    struct spdk_io_channel* ch = bdev_info->ch;

    uint64_t bufsz = 512;

    void* bufwrite = spdk_zmalloc(bufsz, 4096, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    void* bufread = spdk_zmalloc(bufsz, 4096, nullptr, SPDK_ENV_SOCKET_ID_ANY, 1);
    EXPECT_NE(bufwrite, nullptr);
    EXPECT_NE(bufread, nullptr);
    DEFER(spdk_free(bufwrite));
    DEFER(spdk_free(bufread));

    memset(bufwrite, 0x42, bufsz);

    IOVector iov_write, iov_read;
    iov_write.push_back(bufwrite, bufsz);
    iov_read.push_back(bufread, bufsz);

    EXPECT_EQ(photon::spdk::bdev_writev_blocks(desc, ch, iov_write.iovec(), iov_write.iovcnt(), 0, 1), 0);
    EXPECT_EQ(photon::spdk::bdev_readv_blocks(desc, ch, iov_read.iovec(), iov_read.iovcnt(), 0, 1), 0);

    EXPECT_EQ(memcmp(bufwrite, bufread, bufsz), 0);
}


using TimePoint = std::chrono::high_resolution_clock::time_point;

class IOHelper {
public:
    IOHelper(struct spdk_bdev_desc* desc, struct spdk_io_channel* ch) : desc_(desc), ch_(ch) {
        assert(desc_ != nullptr && ch_ != nullptr);
    }

    void IOFunc(void* buf, off_t block_offset, size_t block_count, bool iswrite) {
        int rc = 0;
        if (iswrite) rc = photon::spdk::bdev_write_blocks(desc_, ch_, buf, block_offset, block_count);
        else rc = photon::spdk::bdev_read_blocks(desc_, ch_, buf, block_offset, block_count);
        if (rc != 0) {
            printf("IOFunc, rc=%d\n", rc);
            assert(rc == 0);
        }
    }

    void* GetIOBuffer(uint64_t nblocks, bool iswrite) {
        uint64_t nbytes = nblocks * block_size_;
        void* buf = spdk_zmalloc(nbytes, block_size_, nullptr, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
        if (iswrite) memset(buf, 0x5F, nbytes);
        return buf;
    }

    void FreeIOBuffer(void* buf) { spdk_free(buf); }

    uint64_t GetBlockSize() const { return block_size_;}

private:
    struct spdk_bdev_desc* desc_ = nullptr;
    struct spdk_io_channel* ch_ = nullptr;
    uint64_t block_size_ = 512;
};

TEST_F(SPDKBDevTest, performance) {
    uint64_t nblocks_per_req = 8;
    uint64_t batch_size = 16;
    uint64_t total_blocks = 2097152 * batch_size;
    bool iswrite = false;

    IOHelper io_helper(bdev_info->desc, bdev_info->ch);
    void* buf = io_helper.GetIOBuffer(nblocks_per_req * batch_size, iswrite);
    uint64_t block_size = io_helper.GetBlockSize();
    uint64_t start_block = 0;
    uint64_t remain = total_blocks;

    TimePoint start_time = std::chrono::high_resolution_clock::now();
    while (remain > 0) {
        uint64_t thisone = std::min(remain, nblocks_per_req * batch_size);
        io_helper.IOFunc(buf, start_block, thisone, iswrite);
        remain -= thisone;
        start_block += thisone;
    }
    TimePoint end_time = std::chrono::high_resolution_clock::now();

    io_helper.FreeIOBuffer(buf);

    double cost = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count() / 1000.0; // dura unit is us
    double thp = (total_blocks * block_size / 1024.0 / 1024.0) / (cost / 1e6);
    printf("thp: %.4f MiB/s, cost: %.4f us\n", thp, cost);
}

DEFINE_uint64(bs, 4096, "block size in bytes");
DEFINE_uint64(iodepth, 128, "num of requests on the fly at the same time");
DEFINE_uint32(size, 1, "total access data, unit is GiB");
DEFINE_bool(iswrite, false, "randread or randwrite");

static std::atomic<uint64_t> qps{0};
#define ROUND_DOWN(N, S) ((N) & ~((S) - 1))

TEST_F(SPDKBDevTest, performance2) {
    std::signal(SIGINT, [](int signal){
        if (signal == SIGINT) {
            std::exit(0);
        }
    });

    const uint64_t BLOCKSIZE = FLAGS_bs;
    const uint64_t LBACOUNT = BLOCKSIZE / 512;
    const uint64_t IODEPTH = FLAGS_iodepth;
    const uint32_t TOTALGB = FLAGS_size;
    const bool ISWRITE = FLAGS_iswrite;

    GTEST_LOG_(INFO) << "config: size=" << TOTALGB << "GiB, bs=" << BLOCKSIZE << "(i.e. " << BLOCKSIZE/1024 << "k, " << LBACOUNT << " sectors), iodepth=" << IODEPTH;
    GTEST_LOG_(INFO) << "config: iswrite=" << ISWRITE;

    struct spdk_bdev_desc* desc = bdev_info->desc;
    struct spdk_io_channel* ch = bdev_info->ch;

    auto task_read = [LBACOUNT, BLOCKSIZE, TOTALGB](struct spdk_bdev_desc* desc, struct spdk_io_channel* ch){
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
            EXPECT_EQ(0, photon::spdk::bdev_read_blocks(desc, ch, buf, offset, LBACOUNT));
            qps.fetch_add(1, std::memory_order_relaxed);
        };
    };

    auto task_write = [LBACOUNT, BLOCKSIZE, TOTALGB](struct spdk_bdev_desc* desc, struct spdk_io_channel* ch){
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
            EXPECT_EQ(0, photon::spdk::bdev_write_blocks(desc, ch, buf, offset, LBACOUNT));
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
            photon::thread_create11(task_write, desc, ch);
        }
    }
    else {
        for (int i=0; i<IODEPTH; i++) {
            photon::thread_create11(task_read, desc, ch);
        }
    }


    photon::thread_sleep(-1);
}


int main(int argc, char** argv) {
    testing::AddGlobalTestEnvironment(new SPDKBDevTestEnv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}