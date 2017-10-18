typedef unsigned int uint;

#define CHAR_MAGIC  'C'
#define FILLZERO       _IO(CHAR_MAGIC,1)
#define FILLCHAR       _IOW(CHAR_MAGIC,2,char)
#define GETSIZE        _IOR(CHAR_MAGIC,3,uint)
#define SETSIZE        _IOW(CHAR_MAGIC,4,uint)

#define DRIVE_NAME "char_dev"
