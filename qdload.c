#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <windows.h>
#include "wingetopt.h"
#include "printf.h"
#endif
#include "qcio.h"

// Размер блока загрузки
#define dlblock 1017

//****************************************************
//*  Выделенеи таблиц разделов в отдельные файлы
//****************************************************
void extract_ptable() {
  
unsigned int addr,blk,pg,udsize,ptlen,npar;
unsigned char buf[4096];
FILE* out;

// получаем размер юзерских даных сектора
udsize=(mempeek(nand_cfg0)&(0x3ff<<9))>>9; 

// вынимаем координаты страницы с таблицей
addr=mempeek(nand_addr0)>>16;
if (addr == 0) { 
  // если адреса таблицы нет в регистре - ищем его
  load_ptable(buf,1); 
  addr=mempeek(nand_addr0)>>16;
}  
blk=addr/ppb;
pg=addr%ppb;
nandwait(); // ждем окончания всех предыдущих операций

flash_read(blk, pg, 0);  // сектор 0 - начало таблицы разделов  
memread(buf,sector_buf, udsize);

mempoke(nand_exec,1);     // сектор 1 - продолжение таблицы
nandwait();
memread(buf+udsize,sector_buf, udsize);

// проверяем таблицу
if (memcmp(buf,"\xAA\x73\xEE\x55\xDB\xBD\x5E\xE3",8) != 0) {
   printf("\nТаблица разделов режима чтения не найдена\n");
   return;
}

// Определяем размер и записываем таблицу чтения
npar=*((unsigned int*)&buf[12]); // число разделов в таблице
out=fopen("ptable/current-r.bin","wb");
if (out == 0) {
  printf("\n Ошибка открытия выходного файла ptable/current-r.bin");
  return;
}  
fwrite(buf,16+28*npar,1,out);
printf("\n Найдена таблица разделов режима чтения");
fclose (out);

// Ищем таблицу записи
for (pg=pg+1;pg<ppb;pg++) {
  flash_read(blk, pg, 0);  // сектор 0 - начало таблицы разделов    
  memread(buf,sector_buf, udsize);
  if (memcmp(buf,"\x9a\x1b\x7d\xaa\xbc\x48\x7d\x1f",8) != 0) continue; // сигнатура не найдена - ищем дальше

  // нашли таблицу записи   
  mempoke(nand_exec,1);     // сектор 1 - продолжение таблицы
  nandwait();
  memread(buf+udsize,sector_buf, udsize);
  npar=*((unsigned int*)&buf[12]); // число разделов в таблице
  out=fopen("ptable/current-w.bin","wb");
  if (out == 0) {
    printf("\n Ошибка открытия выходного файла ptable/current-w.bin");
    return;
  }  
  fwrite(buf,16+28*npar,1,out);
  fclose (out);
  printf("\n Найдена таблица разделов режима записи");
  return;
}
printf("\n Таблица разделов режима записи не найдена");
}



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@2
void main(int argc, char* argv[]) {

int opt,res;
unsigned int start=0x41700000;
#ifndef WIN32
char devname[50]="/dev/ttyUSB0";
#else
char devname[50]="";
#endif
FILE* in;
struct stat fstatus;
unsigned int i,partsize,iolen,adr,helloflag=0;
unsigned int sahara_flag=0;
unsigned int tflag=0;

unsigned char iobuf[4096];
unsigned char cmd1[]={0x06};
unsigned char cmd2[]={0x07};
unsigned char cmddl[2048]={0xf};
unsigned char cmdstart[2048]={0x5,0,0,0,0};
unsigned int delay=2;



while ((opt = getopt(argc, argv, "p:k:a:histd:")) != -1) {
  switch (opt) {
   case 'h': 
     printf("\n Утилита предназначена для загрузки программ-прошивальщика (E)NPRG в память модема\n\n\
Допустимы следующие ключи:\n\n\
-p <tty>  - указывает имя устройства последовательного порта, переведенного в download mode\n\
-i        - запускает процедуру HELLO для инициализации загрузчика\n\
-t        - вынимает из модема таблицы разделов в файлы ptable/current-r(w).bin\n\
-s        - использовать протокол SAHARA\n\
-k #           - выбор чипсета: 0(MDM9x15, по умолчанию), 1(MDM8200), 2(MDM9x00), 3(MDM9x25)\n\
-a <adr>  - адрес загрузки, по умолчанию 41700000\n\
-d <n>    - задержка для инициализации загрузчика, 0.1с\n\
\n");
    return;
     
   case 'p':
    strcpy(devname,optarg);
    break;

   case 'k':
     switch(*optarg) {
       case '0':
        nand_cmd=0x1b400000;
	break;

       case '1':
        nand_cmd=0xA0A00000;
	break;

       case '2':
        nand_cmd=0x81200000;
	break;

       case '3':
        nand_cmd=0xf9af0000;
	break;

       default:
	printf("\nНедопустимый номер чипсета\n");
	return;
     }	
    break;

   case 'i':
    helloflag=1;
    break;
    
   case 's':
    sahara_flag=1;
    break;
    
   case 't':
    tflag=1;
    break;
    
   case 'a':
     sscanf(optarg,"%x",&start);
     break;

   case 'd':
     sscanf(optarg,"%i",&delay);
     break;
  }     
}

if ((tflag == 1) && (helloflag == 0)) {
  printf("\n Ключ -t без ключа -i указывать нельзя\n");
  exit(1);
}  

delay*=100000; // переводим в микросекунды
#ifdef WIN32
if (*devname == '\0')
{
   printf("\n - Последовательный порт не задан\n"); 
   return; 
}
#endif

if (!open_port(devname))  {
#ifndef WIN32
   printf("\n - Последовательный порт %s не открывается\n", devname); 
#else
   printf("\n - Последовательный порт COM%s не открывается\n", devname); 
#endif
   return; 
}

// Удаляем старые таблицы разделов

unlink("ptable/current-r.bin");
unlink("ptable/current-w.bin");

//----- Вариант загрузки через сахару -------

if (sahara_flag) {
  if (dload_sahara() == 0) {
	#ifndef WIN32
	usleep(200000);   // ждем инициализации загрузчика
	#else
	Sleep(200);   // ждем инициализации загрузчика
	#endif

	if (helloflag) {
		hello(1);
		printf("\n");
		if (tflag) extract_ptable();  // вынимаем таблицы разделов
	}
  }
  return;
}	

in=fopen(argv[optind],"rb");
if (in == 0) {
  printf("\nОшибка открытия входного файла\n");
  return;
}


//------- Вариант загрузки через запись загрузчика в память ----------

printf("\n Загрузка файла %s\n Адрес загрузки: %08x",argv[optind],start);
iolen=send_cmd_base(cmd1,1,iobuf,1);
if (iolen != 5) {
   printf("\n Модем не находится в режиме загрузки\n");
//   dump(iobuf,iolen,0);
   return;
}   
iolen=send_cmd_base(cmd2,1,iobuf,1);
#ifndef WIN32
fstat(fileno(in),&fstatus);
#else
fstat(_fileno(in),&fstatus);
#endif
printf("\n Размер файла: %i\n",(unsigned int)fstatus.st_size);
partsize=dlblock;

// Цикл поблочной загрузки 
for(i=0;i<fstatus.st_size;i+=dlblock) {  
 if ((fstatus.st_size-i) < dlblock) partsize=fstatus.st_size-i;
 fread(cmddl+7,1,partsize,in);          // читаем блок прямо в командный буфер
 adr=start+i;                           // адрес загрузки этого блока
   // Как обычно у убогих китайцев, числа вписываются через жопу - в формате Big Endian
   // вписываем адрес загрузки этого блока
   cmddl[1]=(adr>>24)&0xff;
   cmddl[2]=(adr>>16)&0xff;
   cmddl[3]=(adr>>8)&0xff;
   cmddl[4]=(adr)&0xff;
   // вписываем размер блока 
   cmddl[5]=(partsize>>8)&0xff;
   cmddl[6]=(partsize)&0xff;
 iolen=send_cmd_base(cmddl,partsize+7,iobuf,1);
 printf("\r Загружено: %i",i+partsize);
// dump(iobuf,iolen,0);
} 
// вписываем адрес в команду запуска
printf("\n Запуск загрузчика..."); fflush(stdout);
cmdstart[1]=(start>>24)&0xff;
cmdstart[2]=(start>>16)&0xff;
cmdstart[3]=(start>>8)&0xff;
cmdstart[4]=(start)&0xff;
iolen=send_cmd_base(cmdstart,5,iobuf,1);
close_port();
#ifndef WIN32
usleep(delay);   // ждем инициализации загрузчика
#else
Sleep(delay/1000);   // ждем инициализации загрузчика
#endif

if (!open_port(devname))  {
#ifndef WIN32
   printf("\n - Последовательный порт %s не открывается\n", devname); 
#else
   printf("\n - Последовательный порт COM%s не открывается\n", devname); 
#endif
   return; 
}
printf("ok\n");

if (helloflag) hello(1);
if (!bad_loader && tflag) extract_ptable();  // вынимаем таблицы разделов
printf("\n");

}

