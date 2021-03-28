#ifndef _APUE_DB_H_
#define _APUE_DB_H_

typedef void * DBHANDLE;

/*
 * 如果成功返回, 将建立两个文件: pathname.idx 和 pathname.dat, 
 * pathname.idx 是索引文件, pathname.dat 是数据文件.
 * 
 * 参数 oflag 作为传递给 open 的第二个参数, 来指定这些文件的打开模式.
 * 
 * 如果需要建立新的数据库, mode 将作为第三个参数传递给 open.
 * 
 * 返回值: 若成功, 返回函数库具柄; 若失败, 返回NULL
 */
DBHANDLE db_open(const char *, int, ...);

/*
 * 当不再使用数据库时, 调用 db_close 来关闭数据库.
 * 
 * db_close 将关闭索引文件和数据文件, 并释放数据库使用过程中分配到的
 * 所有用于内部缓冲区的存储空间.
 */
void db_close(DBHANDLE);

/*
 * 指定键 key 从数据库中获取一条记录
 * 
 * 返回值: 若成功, 返回指向数据的指针; 若没有找到记录, 返回NULL
 */
char *db_fetch(DBHANDLE, const char *);

/*
 * 向数据库中存入一条新的记录
 * 
 * key 和 data 是由 null 字符终止的字符串。
 * flag 参数只能是 DB_INSERT(插入一条新纪录), DB_REPLACE(替换一条已有的记录)
 * 或 DB_STORE(插入一条新纪录或替换一条已有的记录)
 * 
 * 如果使用 DB_REPLACE, 而记录不存, 则将 errno 设置为 ENOENT, 返回值为 -1, 并且不加入新纪录.
 * 如果使用 DB_INSERT, 而记录已经存在, 则不插入新纪录, 返回值为 1.
 * 
 * 返回值: 若成功, 返回 0; 若出错, 返回非 0 值
 */
int db_store(DBHANDLE, const char *, const char *, int);

/*
 * 通过指定 key, 在数据库中删除一条记录
 * 
 * 返回值: 若成功, 返回 0; 若没有找到记录, 返回 -1
 */
int db_delete(DBHANDLE, const char *);

/*
 * 调用 db_rewind 回滚到数据库的第一条记录
 */
void db_rewind(DBHANDLE);

/*
 * 调用 db_nextrec 顺序的读取数据库中的每条记录
 * 如果 key 是非空指针, db_nextrec 将这个指针复制到存储区域开始的内存位置,
 * 然后返回这个指针
 * 
 * db_nextrec 不保证其返回记录的顺序, 只保证对数据库中的每一条记录只读取一次
 * 
 * 返回值: 若成功, 返回指向数据的指针; 若到达数据库文件的尾端, 返回 NULL
 */
char *db_nextrec(DBHANDLE, char *);

/*
 * Flags for db_store()
 */
#define DB_INSERT  1        // insert new record only
#define DB_REPLACE 2        // replace existing record
#define DB_STORE   3        // replace or insert

/*
 * Implementation limits
 */
#define IDXLEN_MIX 6        // key, sep, start, sep, length, \n
#define IDXLEN_MAX 1024     // arbitrary
#define DATLEN_MIN 2        // data byte, newline
#define DATLEN_MAX 1024     // arbitrary

#endif /* _APUE_DB_H_ */
