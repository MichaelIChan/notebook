#include "db.h"

DBHANDLE db_open(const char *pathname, int oflag, ...)
{
    DB *db;
    int len, mode;
    size_t i;
    char asciiptr[PTR_SZ + 1],
         hash[(NHASH_DEF + 1) * PTR_SZ + 2];    // +2 for newline and null
    struct stat statbuff;

    len = strlen(pathname);
    if ((db = _db_alloc(len)) == NULL) {
        err_dump("db_open: _db_alloc error for DB");
    }
    db->nhash   = NHASH_DEF;    // hash table size
    db->hashoff = HASH_OFF;     // offset in index file of hash table
    strcpy(db->name, pathname);
    strcat(db->name, ".idx");

    if (oflag & O_CREAT) {
        va_list ap;

        va_start(ap, oflag);
        mode = va_arg(ap, int);
        va_end(ap);

        // open index file and data file.
        db->idxfd = open(db->name, oflag, mode);
        strcpy(db->name + len, ".dat");
        db->datfd = open(db->name, oflag, mode);
    } else {
        // open index file and data file.
        db->idxfd = open(db->name, oflag);
        strcpy(db->name + len, ".dat");
        db->datfd = open(db->name, oflag);
    }

    if (db->idxfd < 0 || db->datfd < 0) {
        _db_free(db);
        return NULL;
    }

    if ((oflag & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC)) {
        /*
         * If the database was create, we have to initialize it.
         * Write lock the entire file so that we can stat it,
         * check its size, and initialize it, atomically.
         */
        if (writew_lock(db->idxfd, 0, SEEK_SET, 0) < 0) {
            err_dump("dp_open: writew_lock error");
        }

        if (fstat(db->idxfd, &statbuff) < 0) {
            err_sys("db_open: fstat error");
        }

        if (statbuff.st_size == 0) {
            /*
             * We have to build a list of (NHASH_DEF + 1) chain
             * ptrs with a value of 0. The +1 is for the free
             * list pointer that precedes the hash table.
             */
            sprintf(asciiptr, "%*d", PTR_SZ, 0);
            hash[0] = 0;
            for (i = 0; i < NHASH_DEF + 1; i++) {
                strcat(hash, asciiptr);
            }
            strcat(hash, "\n");
            i = strlen(hash);
            if (write(db->idxfd, hash, i) != i) {
                err_dump("dp_open: index file init write error");
            }
        }
        if (un_lock(db->idxfd, 0, SEEK_SET, 0) < 0) {
            err_dump("dp_open: un_lock error");
        }
    }
    db_rewind(db);
    return db;
}

static DB *_db_alloc(int namelen)
{
    DB *db;

    // use calloc, to initialize the structure to zero.
    if ((db = calloc(1, sizeof(DB))) == NULL) {
        err_dump("_db_alloc: calloc error for DB");
    }
    db->idxfd = db->datfd = -1;     // descriptors

    // allocate room for the name, +5 for ".idx" or ".dat" plus null at end.
    if ((db->name = malloc(namelen + 5)) == NULL) {
        err_dump("_db_alloc: malloc error for name");
    }

    // allocate an index buffer and a data buffer,
    // +2 for newline and null at end.
    if ((db->idxbuf = malloc(IDXLEN_MAX + 2)) == NULL) {
        err_dump("_db_alloc: malloc error for index buffer");
    }
    if ((db->datbuf = malloc(DATLEN_MAX + 2)) == NULL) {
        err_dump("_db_alloc: malloc error for data buffer");
    }

    return db;
}

int db_delete(DBHANDLE h, const char *key)
{
    // db_delete用于删除与给定键匹配的一条记录

    DB  *db = h;
    int rc  = 0;

    // 使用_db_find_and_lock来判断在数据库中该记录是否存在,
    // 第三个参数控制对散列表加写锁, 因为可能执行更改该链表的操作
    if (_db_find_and_lock(db, key, 1) == 0) {
        // 存在则调用_db_dodelete函数执行删除该记录的操作
        _db_dodelete(db);
        db->cnt_delok++;
    } else {
        rc = -1;
        db->cnt_delerr++;
    }
    if (un_lock(db->idxfd, db->chainoff, SEEK_SET, 1) < 0) {
        err_dump("db_delete: un_lock error");
    }

    return rc;
}

static void _db_dodelete(DB *db)
{
    // _db_dodelete执行从数据库中删除一条记录的所有操作

    int   i;
    char  *ptr;
    off_t freeptr, saveptr;

    for (ptr = db->datbuf, i = 0; i < db->datlen - 1; i++) {
        *ptr++ = SPACE;
    }
    *ptr = 0;   // null terminate for _db_writedat
    ptr = db->idxbuf;
    while (*ptr) {
        *ptr++ = SPACE;
    }

    // 调用writew_lock对空闲链表加写锁, 防止两个不同进程同时删除不同链表上的记录产生相互影响,
    // 因为要将被删除的记录添加到空闲链表中, 这将改变空闲链表指针
    if (writew_lock(db->idxfd, FREE_OFF, SEEK_SET, 1) < 0) {
        err_dump("_db_dodelete: writew_lock error");
    }

    // 调用_db_writedat清空数据记录,
    // 此时db_delete对这条记录的散列链已经加了写锁, 故这里不需要对数据文件加锁
    _db_writedat(db, db->datbuf, db->datoff, SEEK_SET);

    // 读空闲链表指针
    freeptr = _db_readptr(db, FREE_OFF);

    saveptr = db->ptrval;
}

char *db_fetch(DBHANDLE h, const char *key)
{
    // 函数db_fetch根据给定的键来读取一条记录

    DB   *db = h;
    char *ptr;

    // 调用_db_find_and_lock在数据库中查找记录
    if (_db_find_and_lock(db, key, 0) < 0) {
        // 若不能找到该记录, 则将返回值ptr设置为NULL, 并将不成功的搜索计数器之加1
        ptr = NULL;
        db->cnt_fetcherr++;
    } else {
        // 如果找到了记录, 调用_db_readdat读相应的数据记录, 并将成功记录搜索计数器值加1
        ptr = _db_readdat(db);
        db->cnt_fetchok++;
    }

    if (un_lock(db->idxfd, db->chainoff, SEEK_SET, 1) < 0) {
        err_dump("db_fetch: un_lock error");
    }
    return ptr;
}

static int _db_find_and_lock(DB *db, const char *key, int writelock)
{
    // _db_find_and_lock用于按给定的键查找记录
    // 在搜索记录时, 如果想在索引文件上加一把写锁, 则将writelock参数设置为非0值,
    // 如果将writelock参数设置为0, 则给索引文件上加读锁

    off_t offset, nextoffset;

    // 将键转换为散列值, 用其计算在文件中相应散列链的起始地址(chainoff).
    db->chainoff = (_db_hash(db, key) * PTR_SZ) + db->hashoff;
    db->ptroff = db->chainoff;

    // 等待获得锁, 注意, 只锁该散列链开始处的第一个字节
    if (writelock) {
        if (writew_lock(db->idxfd, db->chainoff, SEEK_SET, 1) < 0) {
            err_dump("_db_find_and_lock: writew_lock error");
        }
    } else {
        if (readw_lock(db->idxfd, db->chainoff, SEEK_SET, 1) < 0) {
            err_dump("_db_find_and_lock: readw_lock error");
        }
    }

    // 调用_db_readptr读散列链中的第一个指针. 如果该函数返回0, 则该散列链为空
    offset = _db_readptr(db, db->ptroff);

    // 遍历散列链中的每一条索引记录, 并比较键, 调用_db_readidx读取每条索引记录.
    // _db_readidx将当前记录的键填入DB结构中的idxbuf字段,
    // 如果_db_readidx返回0, 则已达到散列链的最后一记录项
    while (offset != 0) {
        nextoffset = _db_readidx(db, offset);
        if (strcmp(db->idxbuf, key) == 0) {
            break;      // found a match
        }
        db->ptroff = offset;    // offset of the (unequal) record
        offset = nextoffset;    // next one to compare
    }

    // 如果在循环后, offset为0, 说明已达到散列表末端而且没有找到匹配键, 返回-1
    // 否则, 找到了匹配项, 返回0
    // 此时, ptroff字段包含前一条索引记录的地址, datoff包含数据记录的地址, datlen是数据记录的长度
    // 保存前一条索引记录, 因为必须通过修改前一条记录的链指针以删除当前记录
    return offset == 0 ? -1 : 0;
}

int db_store(DBHANDLE h, const char *key, const char *data, int flag)
{
    DB    *db = h;
    int   rc, keylen, datlen;
    off_t ptrval;

    if (flag != DB_INSERT && flag != DB_REPLACE && flag != DB_STORE) {
        errno = EINVAL;
        return -1;
    }

    keylen = strlen(key);
    datlen = strlen(data) + 1;
    if (datlen < DATLEN_MIN || datlen > DATLEN_MAX) {
        err_dump("db_store: invalid data length");
    }

    // 调用_db_find_and_lock以查看这个记录是否已经存在
    if (_db_find_and_lock(db, key, 1) < 0) {
        // 记录不存在

        if (flag == DB_REPLACE) {
            rc = -1;
            db->cnt_storerr++;
            errno = ENOENT;
            goto doreturn;
        }

        // 读散列链上第一项的偏移量
        ptrval = _db_readptr(db, db->chainoff);

        // 调用_db_findfree在空闲链表中搜索一条已删除的记录, 它的键长度和
        // 数据长度与参数keylen和datlen相同
        if (_db_findfree(db, keylen, datlen) < 0) {
            // 第1种情况
            // 没有找到对应大小的空闲记录, 则将新纪录追加到索引文件和数据文件的末尾
            _db_writedat(db, data, 0, SEEK_END);
            _db_writeidx(db, key, 0, SEEK_END, ptrval);

            // 调用_db_writeptr将新纪录添加到对应的散列链的头部
            _db_writeptr(db, db->chainoff, db->idxoff);
            db->cnt_stor1++;
        } else {
            // 第2种情况
            // _db_findfree找到对应大小的空记录, 并将这条空记录从空闲链表中移除
            // 写入新的索引记录和数据记录, 并将新纪录添加到对应的散列链的头部
            _db_writedat(db, data, db->datoff, SEEK_SET);
            _db_writeidx(db, key, db->idxoff, SEEK_SET, ptrval);
            _db_writeptr(db, db->chainoff, db->idxoff);
            db->cnt_stor2++;
        }
    } else {
        // 记录存在
        
        if (flag == DB_INSERT) {
            rc = 1;
            db->cnt_storerr++;
            goto doreturn;
        }

        if (datlen != db->datlen) {
            // 第3种情况
            // 要替换一条已有记录, 而新数据记录的长度与已有记录的长度不一样
            
            // 调用_db_dodelete删除已有记录, 将该删除记录放在空闲链表头部
            _db_dodelete(db);

            // 调用_db_writedat和_db_writeidx将新记录追加到索引文件和数据文件的末尾
            _db_writedat(db, data, 0, SEEK_END);
            _db_writeidx(db, key, 0, SEEK_END, ptrval);

            // 将新纪录添加到对应的散列表的头部
            _db_writeptr(db, db->chainoff, db->idxoff);
            db->cnt_stor3++;
        } else {
            // 第4种情况
            // 想要替换一条已有记录, 新数据记录的长度与已有记录的长度恰好一样, 此时只需要重写记录即可
            _db_writedat(db, data, db->datoff, SEEK_SET);
            db->cnt_stor4++;
        }
    }
    rc = 0;     // OK

doreturn:
    if (un_lock(db->idxfd, db->chainoff, SEEK_SET, 1) < 0) {
        err_dump("dp_store: un_lock error");
    }
    return rc;
}

static int _db_findfree(DB *db, int keylen, int datlen)
{
    // _db_findfree函数试图找到一个指定大小的空闲索引记录和相关联的数据记录

    int   rc;
    off_t offset, nextoffset, saveoffset;

    // 需要对空闲链表加写锁以避免其他使用空闲链表的进程相互影响
    if (writew_lock(db->idxfd, FREE_OFF, SEEK_SET, 1) < 0) {
        err_dump("_db_findfree: writew_lock error");
    }

    // 得到空闲链表的头指针地址
    saveoffset = FREE_OFF;
    offset = _db_readptr(db, saveoffset);

    // 循环遍历空闲链表以搜寻一个能够匹配键长度和数据长度的索引记录项
    while (offset != 0) {
        nextoffset = _db_readptr(db, offset);
        if (strlen(db->idxbuf) == keylen && db->datlen == datlen) {
            break;
        }
        saveoffset = offset;
        offset = nextoffset;
    }

    if (offset == 0) {
        // 找不到所要求键长度和数据长度的可用记录, 则设置表示失败的返回码
        rc = -1;
    } else {
        // 否则, 将已找到记录的下一个链指针写至前一记录的链表指针, 这样就从空闲链表中移除了该记录
        _db_writeptr(db, saveoffset, db->ptrval);
        rc = 0;
    }

    if (un_lock(db->idxfd, FREE_OFF, SEEK_SET, 1) < 0) {
        err_dump("_db_findfree: un_lock error");
    }
    return rc;
}

void db_close(DBHANDLE h)
{
    _db_free((DB *)h);      // close fds, free buffers & struct
}

static void _db_free(DB *db)
{
    if (db->idxfd >= 0)     { close(db->idxfd); }
    if (db->datfd >= 0)     { close(db->datfd); }
    if (db->idxbuf != NULL) { free(db->idxbuf); }
    if (db->datbuf != NULL) { free(db->datbuf); }
    if (db->name != NULL)   { free(db->name);   }
    free(db);
}

static DBHASH _db_hash(DB *db, const char *key)
{
    DBHASH hval = 0;
    char c;

    for (int i = 0; (c = *key++) != 0; i++) {
        hval += c * i;      // ascii char times its 1-based index
    }
    return (hval % db->nhash);
}

static char *_db_readdat(DB *db)
{
    // 在datoff和datlen已经被正确初始化后, _db_readdat函数将数据记录的内容读入DB结构
    if (lseek(db->datfd, db->datoff, SEEK_SET) == -1) {
        err_dump("_db_readdat: lseek error");
    }
    if (read(db->datfd, db->datbuf, db->datlen) != db->datlen) {
        err_dump("_db_readdat: read error");
    }
    if (db->datbuf[db->datlen - 1] != NEWLINE) {
        err_dump("_db_readdat: missing newline");
    }
    db->datbuf[db->datlen - 1] = 0;     // replace newline with null
    return db->datbuf;
}

static off_t _db_readidx(DB *db, off_t offset)
{
    ssize_t      i;
    char         *ptr1, *ptr2;
    char         asciiptr[PTR_SZ + 1], asciilen[IDXLEN_SZ + 1];
    struct iovec iov[2];

    // 按调用者提供的参数查找索引文件偏移量, 并记录在DB结构中
    if ((db->idxoff = lseek(db->idxfd, offset, offset == 0 ? SEEK_CUR : SEEK_SET)) == -1) {
        err_dump("_db_readix: lseek error");
    }

    // 调用readv读在索引记录开始处的两个定长字段: 指向下一索引记录的链指针和该索引记录余下部分的长度
    iov[0].iov_base = asciiptr;
    iov[0].iov_len  = PTR_SZ;
    iov[1].iov_base = asciilen;
    iov[1].iov_len  = IDXLEN_SZ;
    if ((i = readv(db->idxfd, &iov[0], 2)) != PTR_SZ + IDXLEN_SZ) {
        if (i == 0 && offset == 0) {
            return -1;      // EOF for db_nextree
        }
        err_dump("_db_readidx: readv error of index record");
    }

    // 将下一记录的偏移量转换为整形, 并存放到ptrval字段中, 这将被用作此函数返回值
    asciiptr[PTR_SZ] = 0;           // null terminate
    db->ptrval = toll(asciiptr);    // offset of next key in chain

    // 将索引记录的长度转换为整型，并存放到idxlen字段中
    asciilen[IDXLEN_SZ] = 0;        // null terminate
    if ((db->idxlen = atoi(asciilen)) < IDXLEN_MIX || db->idxlen > IDXLEN_MAX) {
        err_dump("_db_readidx: invalid length");
    }

    // 将索引记录的变长部分读入DB结构中的idxbuf字段. 该记录以null字符代替换行符结尾
    if ((i = read(db->idxfd, db->idxbuf, db->idxlen)) != db->idxlen) {
        err_dump("_db_readidx: read error of index record");
    }
    if (db->idxbuf[db->idxlen - 1] != NEWLINE) {
        err_dump("_db_readidx: missing newline");
    }
    db->idxbuf[db->idxlen - 1] = 0;

    // 将索引记录划分成3个字段: 键, 对应数据记录的偏移量和数据记录的长度
    // strchr函数在给定字符串中找到第一个指定字符
    if ((ptr1 = strchr(db->idxbuf, SEP)) == NULL) {
        err_dump("_db_readix: missing first separator");
    }
    *ptr1++ = 0;        // replace SEP with null
    if ((ptr2 = strchr(ptr1, SEP)) == NULL) {
        err_dump("_db_readix: missing first separator");
    }
    *ptr2++ = 0;        // replace SEP with null

    if (strchr(ptr2, SEP) != NULL) {
        err_dump("_db_readidx: too many separators");
    }

    // 将数据记录偏移量和数据记录长度转换为整型, 并存放在DB结构中
    if ((db->datoff = atol(ptr1)) < 0) {
        err_dump("_db_readidx: starting offset < 0");
    }
    if ((db->datlen = atol(ptr2)) <= 0 || db->datlen > DATLEN_MAX) {
        err_dump("_db_readidx: invalid length");
    }

    // 返回在散列表中下一条记录的偏移量
    return db->ptrval;
}

static off_t _db_readptr(DB *db, off_t offset)
{
    char asciiptr[PTR_SZ + 1];

    if (lseek(db->idxfd, offset, SEEK_SET) == -1) {
        err_dump("_db_readptr: lseek error to ptr field");
    }
    if (read(db->idxfd, asciiptr, PTR_SZ) != PTR_SZ) {
        err_dump("_db_readptr: read error of ptr field");
    }
    asciiptr[PTR_SZ] = 0;       // null terminate
    return (atol(asciiptr));
}

static void _db_writedat(DB *db, const char *data, off_t offset, int whence)
{
    // 当删除一条记录时, 调用函数_db_writedat清空数据记录
    // 当被db_store调用时, 追加写数据文件

    struct iovec iov[2];
    static char  newline = NEWLINE;

    if (whence == SEEK_SET) {
        // 追加写, 需要对文件加锁
        if (writew_lock(db->datfd, 0, SEEK_SET, 0) < 0) {
            err_dump("_db_writedat: writew_lock error");
        }
    }

    // 定位到要写数据记录的位置
    if((db->datoff = lseek(db->datfd, offset, whence)) == -1) {
        err_dump("_db_writedat: lseek error");
    }
    db->datlen = strlen(data) + 1;      /// includes newline

    // 设置iovec数组, 调用writev写数据记录和换行符
    // 不能想当然地认为调用者缓冲区的尾端有空间可以追加换行符,
    // 所以应该将换行符写入另一个缓冲区, 然后再从该缓冲区写至数据记录
    iov[0].iov_base = (char *)data;
    iov[0].iov_len  = db->datlen - 1;
    iov[1].iov_base = &newline;
    iov[1].iov_len  = 1;
    if (writev(db->datfd, &iov[0], 2) != db->datlen) {
        err_dump("_db_writedat: writev error of data record");
    }

    // 如果正在对文件追加一条记录, 那么就释放早先获得的锁
    if (whence == SEEK_END) {
        if (un_lock(db->datfd, 0, SEEK_SET, 0) < 0) {
            err_dump("_db_writedat: un_lock error");
        }
    }
}

static void _db_writeidx(DB *db, const char *key, off_t offset, int whence, off_t ptrval)
{
    // 调用_db_writeidx函数写一条索引记录

    struct iovec iov[2];
    char         asciiptrlen[PTR_SZ + IDXLEN_SZ + 1];
    int          len;

    // 在验证散列链中下一个指针有效后, 创建索引记录, 并将它的后半部分存放到idxbuf中
    if ((db->ptrval = ptrval) < 0 || ptrval > PTR_MAX) {
        err_quit("_db_writeidx: invalid ptr: %d", ptrval);
    }
    sprintf(db->idxbuf, "%s%c%lld%c%ld\n", key, SEP, (long long)db->datoff, SEP, (long)db->datlen);
    // 需要索引记录这一部分的长度以创建该记录的前半部分, 前半部分被存放到局部变量asciiptrlen中
    len = strlen(db->idxbuf);
    if (len < IDXLEN_MIX || len > IDXLEN_MAX) {
        err_dump("_db_writeidx: invalid length");
    }
    sprintf(asciiptrlen, "%*lld%*d", PTR_SZ, (long long)ptrval, IDXLEN_SZ, len);

    // 只有在追加新索引记录时这一函数才需要加锁
    if (whence == SEEK_END) {
        if (writew_lock(db->idxfd, ((db->nhash + 1) * PTR_SZ) + 1, SEEK_SET, 0) < 0) {
            err_dump("_db_writeidx: writew_lock error");
        }
    }

    // 定位到开始写索引记录的位置, 将该偏移量存入DB结构的idxoff字段
    if ((db->idxoff = lseek(db->idxfd, offset, whence)) == -1) {
        err_dump("_db_writeidx: lseek error");
    }

    // 将在两个独立的缓冲区中构建的索引记录存入索引文件中
    iov[0].iov_base = asciiptrlen;
    iov[0].iov_len  = PTR_SZ + IDXLEN_SZ;
    iov[1].iov_base = db->idxbuf;
    iov[1].iov_len  = len;
    if (writev(db->idxfd, &iov[0], 2) != PTR_SZ + IDXLEN_SZ + len) {
        err_dump("_db_writeidx: writev error of index record");
    }

    // 如果是追加写该文件, 则释放在定位操作前获得的锁
    if (whence == SEEK_END) {
        if (un_lock(db->idxfd, ((db->nhash + 1) * PTR_SZ) + 1, SEEK_SET, 0) < 0) {
            err_dump("_db_writeidx: un_lock error");
        }
    }
}

static void _db_writeptr(DB *db, off_t offset, off_t ptrval)
{
    // _db_writeptr用于将以散列链指针写至索引文件中

    char asciiptr[PTR_SZ + 1];

    // 验证ptrval在索引文件的边界范围内, 然后将它转换成ASCII字符串
    if (ptrval < 0 || ptrval > PTR_MAX) {
        err_quit("_db_writeptr: invalid ptr: %d", ptrval);
    }
    sprintf(asciiptr, "%*lld", PTR_SZ, (long long)ptrval);

    // 按指定的偏移量在索引文件中定位, 然后将该指针ASCII字符串写入索引文件
    if (lseek(db->idxfd, offset, SEEK_SET) == -1) {
        err_dump("_db_writeptr: lseek error to ptr field");
    }
    if (write(db->idxfd, asciiptr, PTR_SZ) != PTR_SZ) {
        err_dump("_db_writeptr: write error of ptr field");
    }
}

void db_rewind(DBHANDLE h)
{
    DB    *db = h;
    off_t offset;

    offset = (db->nhash + 1) * PTR_SZ;

    if ((db->idxoff = lseek(db->idxfd, offset + 1, SEEK_SET)) == -1) {
        err_dump("db_rewind: lseek error");     // +1 for newline at end of hash table
    }
}

char *db_nextrec(DBHANDLE h, char *key)
{
    DB   *db = h;
    char c; 
    char *ptr;

    if (readw_lock(db->idxfd, FREE_OFF, SEEK_SET, 1) < 0) {
        err_dump("dp_nextrec: readw_lock error");
    }

    do {
        // 调用_db_readidx读下一个记录
        // 偏移量参数值为0, 以此通知函数从当前偏移量继续读索引记录
        if (_db_readidx(db, 0) < 0) {
            ptr = NULL;
            goto doreturn;
        }

        // 读条读取记录, 会读到已删除的记录, 所以跳过键全是空格的记录
        ptr = db->idxbuf;
        while ((c = *ptr++) != 0 && c == SPACE);
    } while (c == 0);

    if (key != NULL) {
        strcpy(key, db->idxbuf);    // return key
    }
    // 读数据记录, 并将返回值设置为指向包含数据记录的内部缓冲区的指针值
    ptr = _db_readdat(db);
    db->cnt_nextrec++;

doreturn:
    if (un_lock(db->idxfd, FREE_OFF, SEEK_SET, 1) < 0) {
        err_dump("db_nextrec: un_lock error");
    }
    return ptr;
}