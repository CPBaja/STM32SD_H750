/*

 SD - a slightly more friendly wrapper for sdfatlib

 This library aims to expose a subset of SD card functionality
 in the form of a higher level "wrapper" object.

 License: GNU General Public License V3
          (Because sdfatlib is licensed with this.)

 (C) Copyright 2010 SparkFun Electronics

 Modified by Frederic Pillon <frederic.pillon@st.com> for STMicroelectronics

 This library provides four key benefits:

   * Including `STM32SD.h` automatically creates a global
     `SD` object which can be interacted with in a similar
     manner to other standard global objects like `Serial` and `Ethernet`.

   * Boilerplate initialisation code is contained in one method named
     `begin` and no further objects need to be created in order to access
     the SD card.

   * Calls to `open` can supply a full path name including parent
     directories which simplifies interacting with files in subdirectories.

   * Utility methods are provided to determine whether a file exists
     and to create a directory hierarchy.

 */

/*

  Implementation Notes

  In order to handle multi-directory path traversal, functionality that
  requires this ability is implemented as callback functions.

  Individual methods call the `walkPath` function which performs the actual
  directory traversal (swapping between two different directory/file handles
  along the way) and at each level calls the supplied callback function.

  Some types of functionality will take an action at each level (e.g. exists
  or make directory) which others will only take an action at the bottom
  level (e.g. open).

 */

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "stm32_def.h"
}
#include "STM32SD.h"
SDClass SD;

/**
  * @brief  Link SD, register the file system object to the FatFs mode and configure
  *         relatives SD IOs including SD Detect Pin and level if any
  * @param  detect: detect pin number (default SD_DETECT_NONE)
  * @param  level: detect pin level (default SD_DETECT_LEVEL)
  * @retval true or false
  */
bool SDClass::begin(uint32_t detect, uint32_t level)
{
  bool status = false;
  /*##-1- Initializes SD IOs #############################################*/
  if (_card.init(detect, level)) {
    status = _fatFs.init();
  }
  return status;
}

/**
  * @brief  UnLink SD, unregister the file system object and unconfigure
  *         relatives SD IOs including SD Detect Pin and level if any
  * @retval true or false
  */
bool SDClass::end(void)
{
  bool status = false;
  /*##-1- DeInitializes SD IOs ###########################################*/
  if (_fatFs.deinit()) {
    status = _card.deinit();
  }
  return status;
}

/**
  * @brief  Check if a file or folder exist on the SD disk
  * @param  filename: File name
  * @retval true or false
  */
bool SDClass::exists(const char *filepath)
{
  FILINFO fno;

  return (f_stat(filepath, &fno) != FR_OK) ? false : true;
}

/**
  * @brief  Create directory on the SD disk
  * @param  filename: File name
  * @retval true if created or existing else false
  */
bool SDClass::mkdir(const char *filepath)
{
  FRESULT res = f_mkdir(filepath);
  return ((res != FR_OK) && (res != FR_EXIST)) ? false : true;
}

/**
  * @brief  Remove directory on the SD disk
  * @param  filename: File name
  * @retval true or false
  */
bool SDClass::rmdir(const char *filepath)
{
  return (f_unlink(filepath) != FR_OK) ? false : true;
}

/**
  * @brief  Open a file on the SD disk, if not existing it's created
  * @param  filename: File name
  * @param  mode: the mode in which to open the file
  * @retval File object referring to the opened file
  */
File SDClass::open(const char *filepath, uint8_t mode /* = FA_READ */)
{
  File file = File();

  file._name = (char *)malloc(strlen(filepath) + 1);
  if (file._name == NULL) {
    Error_Handler();
  }
  sprintf(file._name, "%s", filepath);

  file._fil = (FIL *)malloc(sizeof(FIL));
  if (file._fil == NULL) {
    Error_Handler();
  }

#if (_FATFS == 68300) || (_FATFS == 80286)
  file._fil->obj.fs = 0;
  file._dir.obj.fs = 0;
#else
  file._fil->fs = 0;
  file._dir.fs = 0;
#endif

  if ((mode == FILE_WRITE) && (!SD.exists(filepath))) {
    mode = mode | FA_CREATE_ALWAYS;
  }

  file._res = f_open(file._fil, filepath, mode);
  if (file._res != FR_OK) {
    free(file._fil);
    file._fil = NULL;
    file._res = f_opendir(&file._dir, filepath);
    if (file._res != FR_OK) {
      free(file._name);
      file._name = NULL;
    }
  }
  return file;
}

/**
  * @brief  Remove a file on the SD disk
  * @param  filename: File name
  * @retval true or false
  */
bool SDClass::remove(const char *filepath)
{
  return (f_unlink(filepath) != FR_OK) ? false : true;
}

File SDClass::openRoot(void)
{
  return open(_fatFs.getRoot());
}

File::File(FRESULT result /* = FR_OK */)
{
  _name = NULL;
  _fil = NULL;
  _res = result;
}

/** List directory contents to Serial.
 *
 * \param[in] flags The inclusive OR of
 *
 * LS_DATE - %Print file modification date
 *
 * LS_SIZE - %Print file size.
 *
 * LS_R - Recursive list of subdirectories.
 *
 * \param[in] indent Amount of space before file name. Used for recursive
 * list to indicate subdirectory level.
 */
void File::ls(uint8_t flags, uint8_t indent)
{
  FRESULT res = FR_OK;
  FILINFO fno;
  char *fn;

#if _USE_LFN
#if (_FATFS == 68300) || (_FATFS == 80286)
  /* altname */
#else
  static char lfn[_MAX_LFN];
  fno.lfname = lfn;
  fno.lfsize = sizeof(lfn);
#endif
#endif

  while (1) {
    res = f_readdir(&_dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0) {
      break;
    }
    if (fno.fname[0] == '.') {
      continue;
    }
#if _USE_LFN && (_FATFS != 68300 && _FATFS != 80286)
    fn = *fno.lfname ? fno.lfname : fno.fname;
#else
    fn = fno.fname;
#endif
    //print any indent spaces
    for (int8_t i = 0; i < indent; i++) {
      Serial.print(' ');
    }
    Serial.print(fn);

    if ((fno.fattrib & AM_DIR) == 0) {
      // print modify date/time if requested
      if (flags & LS_DATE) {
        Serial.print(' ');
        printFatDate(fno.fdate);
        Serial.print(' ');
        printFatTime(fno.ftime);
      }
      // print size if requested
      if (flags & LS_SIZE) {
        Serial.print(' ');
        Serial.print(fno.fsize);
      }
      Serial.println();
    } else {
      // list subdirectory content if requested
      if (flags & LS_R) {
        char *fullPath;
        fullPath = (char *)malloc(strlen(_name) + 1 + strlen(fn) + 1);
        if (fullPath != NULL) {
          sprintf(fullPath, "%s/%s", _name, fn);
          File filtmp = SD.open(fullPath);

          if (filtmp) {
            Serial.println();
            filtmp.ls(flags, indent + 2);
            filtmp.close();
          } else {
            Serial.println(fn);
            Serial.print("Error to open dir: ");
            Serial.println(fn);
          }
          free(fullPath);
        } else {
          Serial.println();
          Serial.print("Error to allocate memory!");
        }
      }
    }
  }
}
//------------------------------------------------------------------------------
/** %Print a directory date field to Serial.
 *
 *  Format is yyyy-mm-dd.
 *
 * \param[in] fatDate The date field from a directory entry.
 */
void File::printFatDate(uint16_t fatDate)
{
  Serial.print(FAT_YEAR(fatDate));
  Serial.print('-');
  printTwoDigits(FAT_MONTH(fatDate));
  Serial.print('-');
  printTwoDigits(FAT_DAY(fatDate));
}
//------------------------------------------------------------------------------
/** %Print a directory time field to Serial.
 *
 * Format is hh:mm:ss.
 *
 * \param[in] fatTime The time field from a directory entry.
 */
void File::printFatTime(uint16_t fatTime)
{
  printTwoDigits(FAT_HOUR(fatTime));
  Serial.print(':');
  printTwoDigits(FAT_MINUTE(fatTime));
  Serial.print(':');
  printTwoDigits(FAT_SECOND(fatTime));
}
//------------------------------------------------------------------------------
/** %Print a value as two digits to Serial.
 *
 * \param[in] v Value to be printed, 0 <= \a v <= 99
 */
void File::printTwoDigits(uint8_t v)
{
  char str[3];
  str[0] = '0' + v / 10;
  str[1] = '0' + v % 10;
  str[2] = 0;
  Serial.print(str);
}

/**
  * @brief  Read byte from the file
  * @retval Byte read
  */
int File::read()
{
  UINT byteread;
  int8_t data;
  return (f_read(_fil, (void *)&data, 1, (UINT *)&byteread) == FR_OK) ? data : -1;
}

/**
  * @brief  Read an amount of data from the file
  * @param  buf: an array to store the read data from the file
  * @param  len: the number of elements to read
  * @retval Number of bytes read
  */
int File::read(void *buf, size_t len)
{
  UINT bytesread;
  return (f_read(_fil, buf, len, (UINT *)&bytesread) == FR_OK) ? bytesread : -1;
}

/**
  * @brief  Close a file on the SD disk
  * @param  None
  * @retval None
  */
void File::close()
{
  if (_name) {
#if (_FATFS == 68300) || (_FATFS == 80286)
    if (_fil) {
      if (_fil->obj.fs != 0) {
#else
    if (_fil) {
      if (_fil->fs != 0) {
#endif
        /* Flush the file before close */
        f_sync(_fil);

        /* Close the file */
        f_close(_fil);
      }
      free(_fil);
      _fil = NULL;
    }

#if (_FATFS == 68300) || (_FATFS == 80286)
    if (_dir.obj.fs != 0) {
#else
    if (_dir.fs != 0) {
#endif
      f_closedir(&_dir);
    }

    free(_name);
    _name = NULL;
  }
}


/**
  * @brief  Ensures that any bytes written to the file are physically saved to the SD card
  * @param  None
  * @retval None
  */
void File::flush()
{
  f_sync(_fil);
}

/**
  * @brief  Read a byte from the file without advancing to the next one
  * @param  None
  * @retval read byte
  */
int File::peek()
{
  int data;
  data = read();
  seek(position() - 1);
  return data;
}

/**
  * @brief  Get the current position within the file
  * @param  None
  * @retval position within file
  */
uint32_t File::position()
{
  uint32_t filepos = 0;
  filepos = f_tell(_fil);
  return filepos;
}

/**
  * @brief  Seek to a new position in the file
  * @param  pos: The position to which to seek
  * @retval true or false
  */
bool File::seek(uint32_t pos)
{
  bool status = false;
  if (pos <= size()) {
    status = (f_lseek(_fil, pos) != FR_OK) ? false : true;
  }
  return status;
}

/**
  * @brief  Get the size of the file
  * @param  None
  * @retval file's size
  */
uint32_t File::size()
{
  uint32_t file_size = 0;

  file_size = f_size(_fil);
  return (file_size);
}

File::operator bool()
{
#if (_FATFS == 68300) || (_FATFS == 80286)
  return !((_name == NULL) || ((_fil == NULL) && (_dir.obj.fs == 0)) || ((_fil != NULL) && (_fil->obj.fs == 0) && (_dir.obj.fs == 0)));
#else
  return !((_name == NULL) || ((_fil == NULL) && (_dir.fs == 0)) || ((_fil != NULL) && (_fil->fs == 0) && (_dir.fs == 0)));
#endif
}
/**
  * @brief  Write data to the file
  * @param  data: Data to write to the file
  * @retval Number of data written (1)
  */
size_t File::write(uint8_t data)
{
  return write(&data, 1);
}

/**
  * @brief  Write an array of data to the file
  * @param  buf: an array of characters or bytes to write to the file
  * @param  len: the number of elements in buf
  * @retval Number of data written
  */
size_t File::write(const char *buf, size_t size)
{
  size_t byteswritten;
  f_write(_fil, (const void *)buf, size, (UINT *)&byteswritten);
  return byteswritten;
}

size_t File::write(const uint8_t *buf, size_t size)
{
  return write((const char *)buf, size);
}

/**
  * @brief  Check if there are any bytes available for reading from the file
  * @retval Number of bytes available
  */
int File::available()
{
  uint32_t n = size() - position();
  return n > 0x7FFF ? 0x7FFF : n;
}


char *File::name()
{
  char *name = strrchr(_name, '/');
  if (name && name[0] == '/') {
    name++;
  }
  return name;
}

/**
  * @brief  Check if the file is directory or normal file
  * @retval true if directory else false
  */
bool File::isDirectory()
{
  // Assume not a directory
  bool status = false;
  FILINFO fno;
  if (_name == NULL) {
    Error_Handler();
  }
#if (_FATFS == 68300) || (_FATFS == 80286)
  if (_dir.obj.fs != 0)
#else
  if (_dir.fs != 0)
#endif
  {
    status = true;
  }
#if (_FATFS == 68300) || (_FATFS == 80286)
  else if (_fil->obj.fs != 0)
#else
  else if (_fil->fs != 0)
#endif
  {
    status = false;
  } else {
    // if not init get info
    if (f_stat(_name, &fno) == FR_OK) {
      if (fno.fattrib & AM_DIR) {
        status = true;
      }
    }
  }
  return status;
}

File File::openNextFile(uint8_t mode)
{
  FRESULT res = FR_OK;
  FILINFO fno;
  char *fn;
#if _USE_LFN && (_FATFS != 68300 && _FATFS != 80286)
  static char lfn[_MAX_LFN];
  fno.lfname = lfn;
  fno.lfsize = sizeof(lfn);
#endif
  bool found = false;
  File filtmp = File();
  while (!found) {
    res = f_readdir(&_dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0) {
      filtmp._res = res;
      found = true;
    } else {
      if (fno.fname[0] == '.') {
        continue;
      }
#if _USE_LFN && (_FATFS != 68300 && _FATFS != 80286)
      fn = *fno.lfname ? fno.lfname : fno.fname;
#else
      fn = fno.fname;
#endif
      size_t name_len = strlen(_name);
      char *fullPath = (char *)malloc(name_len + strlen(fn) + 2);
      if (fullPath != NULL) {
        // Avoid twice '/'
        if ((name_len > 0)  && (_name[name_len - 1] == '/')) {
          sprintf(fullPath, "%s%s", _name, fn);
        } else {
          sprintf(fullPath, "%s/%s", _name, fn);
        }
        filtmp = SD.open(fullPath, mode);
        free(fullPath);
        found = true;
      } else {
        filtmp._res = FR_NOT_ENOUGH_CORE;
        found = true;
      }
    }
  }
  return filtmp;
}

void File::rewindDirectory(void)
{
  if (isDirectory()) {
#if (_FATFS == 68300) || (_FATFS == 80286)
    if (_dir.obj.fs != 0) {
#else
    if (_dir.fs != 0) {
#endif
      f_closedir(&_dir);
    }
    f_opendir(&_dir, _name);
  }
}

