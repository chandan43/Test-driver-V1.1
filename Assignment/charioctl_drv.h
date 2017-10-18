
#define CHAR_MAGIC  'C'
#define FILE_READ       _IOR(CHAR_MAGIC,1,char*)
#define FILE_WRITE      _IOW(CHAR_MAGIC,2,char*)

#define DRIVE_NAME "char_dvr"
