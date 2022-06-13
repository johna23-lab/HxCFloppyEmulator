void trackseek     (unsigned char drive,unsigned char track,unsigned char head);
void calibratedrive(unsigned char drive);
int  write_sector(unsigned char deleted,unsigned index,unsigned char drive,unsigned char head, unsigned char track,unsigned char sector,unsigned char nbsector,unsigned int size,unsigned char density,unsigned char precomp,int rate,int gap3);
int  read_sector (unsigned char deleted,unsigned index,unsigned char drive,unsigned char head, unsigned char track,unsigned char sector,unsigned char nbsector,unsigned int size,unsigned char density,int rate,int gap3);
int  format_track(int drive,unsigned char density,int track,int head,int nbsector,int sectorsize,int interleave,unsigned char formatvalue,unsigned char precomp,int rate,int gap3);
int  chs_biosdisk (int cmd, int drive, int head, int track,int sector, int nsects, void *buf);
void init_floppyio(void);
void fdc_specify(unsigned char t);
void reset_drive(unsigned char drive);
int  fd_result(int sensei);
void selectdrive(unsigned char drive);

