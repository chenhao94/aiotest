#include "iotest.hpp"

using namespace std;

size_t thread_num = 1;
size_t testType;
size_t workload;
size_t FILE_SIZE = 1 << 31;
size_t READ_SIZE = 1 << 12;
size_t WRITE_SIZE = 1 << 12;
size_t IO_ROUND = 1 << 10;
size_t SYNC_RATE = 1;
size_t WAIT_RATE = 1;

#ifdef __linux__
io_context_t LibAIOWrite::io_cxt;
#endif

void RandomWrite::run_writeonly(size_t thread_id)
{
    //auto file = fopen(("tmp/file" + to_string(thread_id)).data(), "r+");
    fd = open(("tmp/file" + to_string(thread_id)).data(), openflags); //fileno(file);
    tai::register_fd(fd, "tmp/file" + to_string(thread_id));
    //vector<char> data(WRITE_SIZE, 'a');
    auto data = new(align_val_t(512)) char[WRITE_SIZE];
    memset(data, 'a', sizeof(WRITE_SIZE));
    reset_cb();
    for (size_t i = 0; i < IO_ROUND; ++i)
    {
        if (i && !(i & ~-SYNC_RATE))
        {
            syncop();
            if (!(i & ~-WAIT_RATE))
                wait_cb();
        }
        auto offset = randgen(max(READ_SIZE, WRITE_SIZE));
        writeop(offset, data);
        if (!i || i * 30 / IO_ROUND > (i - 1) * 30 / IO_ROUND)
            tai::Log::log("[Thread ", thread_id, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
    }
    syncop();
    wait_cb();
    cleanup();
    tai::deregister_fd(fd);
    //fclose(file);
    close(fd);
    operator delete[](data, align_val_t(512));
}

void RandomWrite::run_readonly(size_t thread_id)
{
    fd = open(("tmp/file" + to_string(thread_id)).data(), openflags); 
    tai::register_fd(fd, "tmp/file" + to_string(thread_id));
    auto buf = new(align_val_t(512)) char[WRITE_SIZE * WAIT_RATE];
    reset_cb();
    for (size_t i = 0; i < IO_ROUND; ++i)
    {
        if (i && !(i & ~-WAIT_RATE))
        {
            wait_cb();
        }
        auto offset = randgen(max(READ_SIZE, WRITE_SIZE));
        readop(offset, buf + (i & ~-WAIT_RATE) * WRITE_SIZE);
        if (!i || i * 30 / IO_ROUND > (i - 1) * 30 / IO_ROUND)
            tai::Log::log("[Thread ", thread_id, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
    }
    //syncop();
    wait_cb();
    cleanup();
    tai::deregister_fd(fd);
    close(fd);
    operator delete[](buf, align_val_t(512));
}

void RandomWrite::run_readwrite(size_t thread_id)
{
    fd = open(("tmp/file" + to_string(thread_id)).data(), openflags); 
    tai::register_fd(fd, "tmp/file" + to_string(thread_id));
    auto data = new(align_val_t(512)) char[WRITE_SIZE];
    auto buf = new(align_val_t(512)) char[WRITE_SIZE * WAIT_RATE];
    vector<size_t> offs;
    offs.reserve(IO_ROUND);
    memset(data, 'a', sizeof(WRITE_SIZE));
    reset_cb();
    for (size_t i = 0; i < IO_ROUND; ++i)
    {
        if (i && !(i & ~-SYNC_RATE))
        {
            syncop();
            if (!(i & ~-WAIT_RATE))
            {
                for (auto j = i - WAIT_RATE; j < i; ++j)
                    readop(offs[j], buf + (j & ~-WAIT_RATE) * WRITE_SIZE);
                wait_cb();
            }
        }
        auto offset = randgen(max(READ_SIZE, WRITE_SIZE));
        offs.push_back(offset);
        writeop(offset, data);
        if (!i || i * 30 / IO_ROUND > (i - 1) * 30 / IO_ROUND)
            tai::Log::log("[Thread ", thread_id, "]", "Progess ", i * 100 / IO_ROUND, "\% finished.");
    }
    for (auto j = IO_ROUND - WAIT_RATE; j < IO_ROUND; ++j)
        readop(offs[j], buf + (j & ~-WAIT_RATE) * WRITE_SIZE);
    syncop();
    wait_cb();
    cleanup();
    tai::deregister_fd(fd);
    close(fd);
    operator delete[](data, align_val_t(512));
    operator delete[](buf, align_val_t(512));
}

void RandomWrite::run(size_t thread_id)
{
    array<function<void(size_t)>, 3>{
    [this](auto _){ run_readonly(_); },
    [this](auto _){ run_writeonly(_); },
    [this](auto _){ run_readwrite(_); }
}[workload](thread_id);
}

void BlockingWrite::writeop(off_t offset, char* data){
    if (lseek(fd, offset, SEEK_SET) < 0)
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at lseek." << endl;
        exit(-1);
    }
    if (write(fd, data, WRITE_SIZE) < 0)
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at write." << endl;
        exit(-1);
    }
}

void BlockingWrite::readop(off_t offset, char* data)
{
    if (lseek(fd, offset, SEEK_SET) < 0)
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at lseek." << endl;
        exit(-1);
    }
    if (read(fd, data, READ_SIZE) < 0)
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at read." << endl;
        exit(-1);
    }
}

void BlockingWrite::syncop()
{
    if (fsync(fd))
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at fsync." << endl;
        exit(-1);
    }
}

void BlockingWrite::startEntry(size_t thread_id, int flags)
{
    BlockingWrite bw;
    bw.openflags |= flags;
    bw.run(thread_id);
}

void AIOWrite::cleanup() 
{
    for (auto &i : cbs)
        if (unlikely(aio_error(&i) && aio_error(&i) != EINPROGRESS))
        {
            cerr << aio_error(&i) <<  " Error " << errno << ": " << strerror(errno) << " at aio_error." << endl;
            cerr << "reqprio: " << i.aio_reqprio << ", offset: " << i.aio_offset << " , nbytes: " << i.aio_nbytes << endl; 
            exit(-1);
        }
        else
            aio_return(&i);
}

void AIOWrite::writeop(off_t offset, char* data)
{
    cbs.emplace_back();
    auto& cb = cbs.back();
    memset(&cb, 0, sizeof(aiocb));
    cb.aio_fildes = fd;
    cb.aio_nbytes = WRITE_SIZE;
    cb.aio_buf = data;
    cb.aio_offset = offset;
    if (aio_write(&cb))
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at aio_write." << endl;
        exit(-1);
    }
}

void AIOWrite::readop(off_t offset, char* data) 
{
    cbs.emplace_back();
    auto& cb = cbs.back();
    memset(&cb, 0, sizeof(aiocb));
    cb.aio_fildes = fd;
    cb.aio_nbytes = READ_SIZE;
    cb.aio_buf = data;
    cb.aio_offset = offset;
    if (aio_read(&cb))
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at aio_read." << endl;
        exit(-1);
    }
}

void AIOWrite::syncop() 
{
    cbs.emplace_back();
    auto& cb = cbs.back();
    memset(&cb, 0, sizeof(aiocb));
    cb.aio_fildes = fd;
    if (aio_fsync(O_SYNC, &cb))
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at aio_fsync." << endl;
        exit(-1);
    }

}

void AIOWrite::startEntry(size_t thread_id)
{
    AIOWrite aw;
    aw.run(thread_id);
}

void LibAIOWrite::reset_cb() 
{
    #ifdef __linux__
    cbs.clear();
    #endif
}

void LibAIOWrite::wait_cb()
{
    #ifdef __linux__
    io_getevents(io_cxt, cnt, cnt, events, NULL);
    #endif
    cnt = 0;
}

void LibAIOWrite::writeop(off_t offset, char* data) 
{
    #ifdef __linux__
    cbs.emplace_back(new iocb);
    auto& cb = cbs.back();
    io_prep_pwrite(cb, fd, data, WRITE_SIZE, offset);
    auto err = io_submit(io_cxt, 1, &cb);
    if (err < 1)
    {
        cerr << "Error " << -err << ": " << strerror(-err) << " at libaio: write." << endl;
        exit(-1);
    }
    #endif
    ++cnt;
}

void LibAIOWrite::readop(off_t offset, char* data)
{
    #ifdef __linux__
    cbs.emplace_back(new iocb);
    auto& cb = cbs.back();
    io_prep_pread(cb, fd, data, READ_SIZE, offset);
    auto err = io_submit(io_cxt, 1, &cb);
    if (err < 1)
    {
        cerr << "Error " << -err << ": " << strerror(-err) << " at libaio: read." << endl;
        exit(-1);
    }
    #endif
    ++cnt;
}

void LibAIOWrite::syncop() 
{
}

void LibAIOWrite::startEntry(size_t thread_id)
{
    LibAIOWrite lw;
    lw.run(thread_id);
}

void TAIWrite::cleanup() 
{
    for (auto &i : cbs)
        if (unlikely(tai::aio_error(&i) && tai::aio_error(&i) != EINPROGRESS))
        {
            cerr << tai::aio_error(&i) <<  " Error " << errno << ": " << strerror(errno) << " at aio_error." << endl;
            cerr << "reqprio: " << i.aio_reqprio << ", offset: " << i.aio_offset << " , nbytes: " << i.aio_nbytes << endl; 
            exit(-1);
        }
        else
            tai::aio_return(&i);
    if (fsync(fd))
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at fsync." << endl;
        exit(-1);
    }
}

void TAIWrite::writeop(off_t offset, char* data) 
{
    cbs.emplace_back(fd, offset, data, WRITE_SIZE);
    if (tai::aio_write(&cbs.back()))
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at aio_write." << endl;
        exit(-1);
    }
}

void TAIWrite::readop(off_t offset, char* data) 
{
    cbs.emplace_back(fd, offset, data, READ_SIZE);
    if (tai::aio_read(&cbs.back()))
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at aio_read." << endl;
        exit(-1);
    }
}

void TAIWrite::syncop()
{
    cbs.emplace_back(fd);
    if (tai::aio_fsync(O_SYNC, &cbs.back()))
    {
        cerr << "Error " << errno << ": " << strerror(errno) << " at aio_fsync." << endl;
        exit(-1);
    }
}

void TAIWrite::startEntry(size_t thread_id)
{
    TAIWrite tw;
    tw.run(thread_id);
}

