/*
***************************************************************************
*
* Author: Teunis van Beelen
*
* Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014 Teunis van Beelen
*
* teuniz@gmail.com
*
***************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation version 2 of the License.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*
***************************************************************************
*
* This version of GPL is at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*
***************************************************************************
*/



#include "edfplusd_cnv.h"



#if defined(__APPLE__) || defined(__MACH__) || defined(__APPLE_CC__)

#define fopeno fopen

#else

#define fseeko fseeko64
#define ftello ftello64
#define fopeno fopen64

#endif





UI_EDFDwindow::UI_EDFDwindow(char *recent_ope_dir, char *recent_sav_dir)
{
  char txt_string[2048];

  recent_opendir = recent_ope_dir;
  recent_savedir = recent_sav_dir;

  myobjectDialog = new QDialog;

  myobjectDialog->setMinimumSize(QSize(600, 480));
  myobjectDialog->setMaximumSize(QSize(600, 480));
  myobjectDialog->setWindowTitle("EDF+D/BDF+D to EDF+C/BDF+C converter");
  myobjectDialog->setModal(TRUE);
  myobjectDialog->setAttribute(Qt::WA_DeleteOnClose, TRUE);

  pushButton1 = new QPushButton(myobjectDialog);
  pushButton1->setGeometry(QRect(20, 430, 100, 26));
  pushButton1->setText("Select File");

  pushButton2 = new QPushButton(myobjectDialog);
  pushButton2->setGeometry(QRect(480, 430, 100, 26));
  pushButton2->setText("Close");

  textEdit1 = new QTextEdit(myobjectDialog);
  textEdit1->setGeometry(QRect(20, 20, 560, 380));
  textEdit1->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  textEdit1->setReadOnly(TRUE);
  textEdit1->setLineWrapMode(QTextEdit::NoWrap);
  sprintf(txt_string, "EDF+D/BDF+D to EDF+C/BDF+C converter.\n");
  textEdit1->append(txt_string);

  QObject::connect(pushButton1, SIGNAL(clicked()), this, SLOT(SelectFileButton()));
  QObject::connect(pushButton2, SIGNAL(clicked()), myobjectDialog, SLOT(close()));

  myobjectDialog->exec();
}



void UI_EDFDwindow::SelectFileButton()
{
  FILE *inputfile=NULL,
       *outputfile=NULL;

  int i,
      file_number,
      offset,
      offset_2,
      datarecords,
      datarecords_read,
      datarecords_written,
      cnt,
      annot_signal_size,
      annot_signal_size_2,
      annot_signal_nr,
      annot_signal_nr_2,
      annots_written,
      len,
      progress_steps;

  long long former_timestamp,
            next_timestamp,
            new_hdr_timestamp,
            chunk_starttime,
            chunk_endtime;

  char txt_string[2048],
       inputpath[MAX_PATH_LENGTH],
       output_path[MAX_PATH_LENGTH],
       *fileheader,
       *readbuf,
       *tal;

  struct annotationblock *annotationlist[1],
                         *annotblock;

  struct edfhdrblock *edfhdr=NULL;


  pushButton1->setEnabled(FALSE);

  strcpy(inputpath, QFileDialog::getOpenFileName(0, "Select inputfile", QString::fromLocal8Bit(recent_opendir), "EDF/BDF files (*.edf *.EDF *.bdf *.BDF *.rec *.REC)").toLocal8Bit().data());

  if(!strcmp(inputpath, ""))
  {
    pushButton1->setEnabled(TRUE);
    return;
  }

  get_directory_from_path(recent_opendir, inputpath, MAX_PATH_LENGTH);

  inputfile = fopeno(inputpath, "rb");
  if(inputfile==NULL)
  {
    snprintf(txt_string, 2048, "Error, can not open file %s for reading.\n", inputpath);
    textEdit1->append(QString::fromLocal8Bit(txt_string));
    pushButton1->setEnabled(TRUE);
    return;
  }

  snprintf(txt_string, 2048, "Processing file %s", inputpath);
  textEdit1->append(txt_string);

/***************** check if the file is valid ******************************/

  EDFfileCheck EDFfilechecker;

  edfhdr = EDFfilechecker.check_edf_file(inputfile, txt_string);
  if(edfhdr==NULL)
  {
    fclose(inputfile);
    textEdit1->append("Error, file is not a valid EDF or BDF file.\n");
    pushButton1->setEnabled(TRUE);
    return;
  }

  if(((!edfhdr->edfplus)&&(!edfhdr->bdfplus))||(!edfhdr->discontinuous))
  {
    free(edfhdr->edfparam);
    free(edfhdr);
    fclose(inputfile);
    textEdit1->append("Error, file is not an EDF+D or BDF+D file.\n");
    pushButton1->setEnabled(TRUE);
    return;
  }

/****************** get annotations ******************************/

  edfhdr->file_hdl = inputfile;

  EDF_annotations annotations_func;

  annotationlist[0] = NULL;

  if(annotations_func.get_annotations(0, edfhdr, &annotationlist[0], 0))
  {
    free_annotations(annotationlist[0]);
    free(edfhdr->edfparam);
    free(edfhdr);
    fclose(inputfile);
    textEdit1->append("Error, there is an incompatibility with the annotations.\n");
    pushButton1->setEnabled(TRUE);
    return;
  }

  if(edfhdr->annots_not_read)
  {
    free_annotations(annotationlist[0]);
    free(edfhdr->edfparam);
    free(edfhdr);
    fclose(inputfile);
    textEdit1->append("Aborted.\n");
    pushButton1->setEnabled(TRUE);
    return;
  }

  annotblock = annotationlist[0];

/***************** start conversion ******************************/

  datarecords = edfhdr->datarecords;

  annot_signal_nr = edfhdr->annot_ch[0];

  offset = edfhdr->edfparam[annot_signal_nr].buf_offset;

  if(edfhdr->edfplus)
  {
    annot_signal_size = edfhdr->edfparam[annot_signal_nr].smp_per_record * 2;
  }
  else
  {
    annot_signal_size = edfhdr->edfparam[annot_signal_nr].smp_per_record * 3;
  }

  fileheader = (char *)calloc(1, edfhdr->hdrsize);
  if(fileheader==NULL)
  {
    textEdit1->append("Malloc error, (fileheader).\n");
    free_annotations(annotationlist[0]);
    free(edfhdr->edfparam);
    free(edfhdr);
    fclose(inputfile);
    pushButton1->setEnabled(TRUE);
    return;
  }

  readbuf = (char *)calloc(1, edfhdr->recordsize);
  if(readbuf==NULL)
  {
    textEdit1->append("Malloc error, (readbuf).\n");
    free_annotations(annotationlist[0]);
    free(edfhdr->edfparam);
    free(edfhdr);
    free(fileheader);
    fclose(inputfile);
    pushButton1->setEnabled(TRUE);
    return;
  }

  rewind(inputfile);
  if(fread(fileheader, edfhdr->hdrsize, 1, inputfile) != 1)
  {
    textEdit1->append("Read error.\n");
    free_annotations(annotationlist[0]);
    free(edfhdr->edfparam);
    free(edfhdr);
    free(fileheader);
    free(readbuf);
    fclose(inputfile);
    pushButton1->setEnabled(TRUE);
    return;
  }

///////////////////////////////////////////////////////////////////

  file_number = 1;

  strcpy(output_path, inputpath);
  remove_extension_from_filename(output_path);
  if(edfhdr->edfplus)
  {
    sprintf(output_path + strlen(output_path), "_%04i.edf", file_number);
  }
  else
  {
    sprintf(output_path + strlen(output_path), "_%04i.bdf", file_number);
  }

  strcpy(txt_string, "Creating file ");
  len = strlen(txt_string);
  get_filename_from_path(txt_string + len, output_path, MAX_PATH_LENGTH - len);
  textEdit1->append(QString::fromLocal8Bit(txt_string));

  outputfile = fopeno(output_path, "w+b");
  if(outputfile==NULL)
  {
    textEdit1->append("Error, can not open outputfile for writing.\n");
    free_annotations(annotationlist[0]);
    free(edfhdr->edfparam);
    free(edfhdr);
    free(fileheader);
    free(readbuf);
    fclose(inputfile);
    pushButton1->setEnabled(TRUE);
    return;
  }

  if(fwrite(fileheader, edfhdr->hdrsize, 1, outputfile) != 1)
  {
    textEdit1->append("Write error.\n");
    free_annotations(annotationlist[0]);
    free(edfhdr->edfparam);
    free(edfhdr);
    free(fileheader);
    free(readbuf);
    fclose(inputfile);
    fclose(outputfile);
    pushButton1->setEnabled(TRUE);
    return;
  }

  QProgressDialog progress("Processing file...", "Abort", 0, datarecords);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(200);

  progress_steps = datarecords / 100;
  if(progress_steps < 1)
  {
    progress_steps = 1;
  }

  datarecords_written = 0;

  new_hdr_timestamp = 0LL;

  former_timestamp = 0LL;

  next_timestamp = 0LL;

  chunk_starttime = 0LL;

  chunk_endtime = 0LL;

  tal = readbuf + offset;

  fseeko(inputfile, (long long)(edfhdr->hdrsize), SEEK_SET);

  for(datarecords_read=0; datarecords_read<datarecords; datarecords_read++)
  {
    if(!(datarecords_read%progress_steps))
    {
      progress.setValue(datarecords_read);

      qApp->processEvents();

      if(progress.wasCanceled() == TRUE)
      {
        textEdit1->append("Aborted.\n");

        break;
      }
    }

    if(fread(readbuf, edfhdr->recordsize, 1, inputfile) != 1)
    {
      progress.reset();
      textEdit1->append("Read error.\n");
      free_annotations(annotationlist[0]);
      free(edfhdr->edfparam);
      free(edfhdr);
      free(fileheader);
      free(readbuf);
      fclose(inputfile);
      fclose(outputfile);
      pushButton1->setEnabled(TRUE);
      return;
    }

    if(edfhdr->nr_annot_chns>1)
    {
      for(i=1; i<edfhdr->nr_annot_chns; i++)
      {
        annot_signal_nr_2 = edfhdr->annot_ch[i];

        offset_2 = edfhdr->edfparam[annot_signal_nr_2].buf_offset;

        if(edfhdr->edfplus)
        {
          annot_signal_size_2 = edfhdr->edfparam[annot_signal_nr_2].smp_per_record * 2;
        }
        else
        {
          annot_signal_size_2 = edfhdr->edfparam[annot_signal_nr_2].smp_per_record * 3;
        }

        memset(readbuf + offset_2, 0, annot_signal_size_2);
      }
    }

    next_timestamp = get_datarecord_timestamp(tal);

    if(!datarecords_read)
    {
      former_timestamp = next_timestamp - edfhdr->long_data_record_duration;
    }

    if(next_timestamp!=(former_timestamp + edfhdr->long_data_record_duration))
    {
      chunk_endtime = next_timestamp;

      write_values_to_hdr(outputfile, new_hdr_timestamp, datarecords_written, edfhdr);

      annotblock = annotationlist[0];

      annots_written = 0;

      if(annotblock!=NULL)
      {
        while(annotblock!=NULL)
        {
          if(((annotblock->onset>=chunk_starttime)||(file_number==1))
             &&(annotblock->onset<chunk_endtime)
             &&(annots_written<datarecords_written))
          {
            fseeko(outputfile, (long long)(edfhdr->hdrsize + (annots_written * edfhdr->recordsize) + offset), SEEK_SET);

            for(cnt=1; ; cnt++)
            {
              if(fgetc(outputfile)==0)  break;
            }

            fseeko(outputfile, 0LL, SEEK_CUR);

            cnt += fprintf(outputfile, "+%i", (int)((annotblock->onset - new_hdr_timestamp) / TIME_DIMENSION));

            if(annotblock->onset%TIME_DIMENSION)
            {
              cnt += fprintf(outputfile, ".%07i", (int)((annotblock->onset - new_hdr_timestamp) % TIME_DIMENSION));

              fseeko(outputfile, -1LL, SEEK_CUR);

              while(fgetc(outputfile)=='0')
              {
                fseeko(outputfile, -2LL, SEEK_CUR);

                cnt--;
              }
            }

            fseeko(outputfile, 0LL, SEEK_CUR);

            if(annotblock->duration[0]!=0)
            {
              fputc(21, outputfile);

              cnt++;

              cnt += fprintf(outputfile, "%s", annotblock->duration);
            }

            fputc(20, outputfile);

            cnt++;

            for(i = 0; i < (annot_signal_size - cnt - 2); i++)
            {
             if(annotblock->annotation[i]==0)  break;

             fputc(annotblock->annotation[i], outputfile);

             cnt++;
            }

            fputc(20, outputfile);

            cnt++;

            for(i = cnt; i < annot_signal_size; i++)
            {
              fputc(0, outputfile);
            }

            annots_written++;
          }

          annotblock = annotblock->next_annotation;
        }
      }

      fclose(outputfile);

      datarecords_written = 0;

      new_hdr_timestamp = next_timestamp;

      new_hdr_timestamp -= next_timestamp % TIME_DIMENSION;

      chunk_starttime = next_timestamp;

      file_number++;

      strcpy(output_path, inputpath);
      remove_extension_from_filename(output_path);
      if(edfhdr->edfplus)
      {
        sprintf(output_path + strlen(output_path), "_%04i.edf", file_number);
      }
      else
      {
        sprintf(output_path + strlen(output_path), "_%04i.bdf", file_number);
      }

      strcpy(txt_string, "Creating file ");
      len = strlen(txt_string);
      get_filename_from_path(txt_string + len, output_path, MAX_PATH_LENGTH - len);
      textEdit1->append(txt_string);

      outputfile = fopeno(output_path, "w+b");
      if(outputfile==NULL)
      {
        textEdit1->append("Error, can not open outputfile for writing.\n");
        free_annotations(annotationlist[0]);
        free(edfhdr->edfparam);
        free(edfhdr);
        free(fileheader);
        free(readbuf);
        fclose(inputfile);
        pushButton1->setEnabled(TRUE);
        return;
      }

      if(fwrite(fileheader, edfhdr->hdrsize, 1, outputfile) != 1)
      {
        textEdit1->append("Write error.\n");
        free_annotations(annotationlist[0]);
        free(edfhdr->edfparam);
        free(edfhdr);
        free(fileheader);
        free(readbuf);
        fclose(inputfile);
        fclose(outputfile);
        pushButton1->setEnabled(TRUE);
        return;
      }
    }

    cnt = sprintf(tal, "+%i", (int)((next_timestamp - new_hdr_timestamp) / TIME_DIMENSION));

    if((next_timestamp - new_hdr_timestamp)%TIME_DIMENSION)
    {
      cnt += sprintf(tal + cnt, ".%07i", (int)((next_timestamp - new_hdr_timestamp) % TIME_DIMENSION));

      for(i=cnt-1; i>0; i--)
      {
        if(tal[i]!='0')  break;

        cnt--;
      }
    }

    tal[cnt++] = 20;
    tal[cnt++] = 20;

    for(i=cnt; i<annot_signal_size; i++)
    {
      tal[i] = 0;
    }

    if(fwrite(readbuf, edfhdr->recordsize, 1, outputfile) != 1)
    {
      progress.reset();
      textEdit1->append("Write error.\n");
      free_annotations(annotationlist[0]);
      free(edfhdr->edfparam);
      free(edfhdr);
      free(fileheader);
      free(readbuf);
      fclose(inputfile);
      fclose(outputfile);
      pushButton1->setEnabled(TRUE);
      return;
    }

    datarecords_written++;

    former_timestamp = next_timestamp;
  }

  write_values_to_hdr(outputfile, new_hdr_timestamp, datarecords_written, edfhdr);

  annotblock = annotationlist[0];

  annots_written = 0;

  if(annotblock!=NULL)
  {
    while(annotblock!=NULL)
    {
      if((annotblock->onset>=chunk_starttime)&&(annots_written<datarecords_written))
      {
        fseeko(outputfile, (long long)(edfhdr->hdrsize + (annots_written * edfhdr->recordsize) + offset), SEEK_SET);

        for(cnt=1; ; cnt++)
        {
          if(fgetc(outputfile)==0)  break;
        }

        fseeko(outputfile, 0LL, SEEK_CUR);

        cnt += fprintf(outputfile, "+%i", (int)((annotblock->onset - new_hdr_timestamp) / TIME_DIMENSION));

        if(annotblock->onset%TIME_DIMENSION)
        {
          cnt += fprintf(outputfile, ".%07i", (int)((annotblock->onset - new_hdr_timestamp) % TIME_DIMENSION));

          fseeko(outputfile, -1LL, SEEK_CUR);

          while(fgetc(outputfile)=='0')
          {
            fseeko(outputfile, -2LL, SEEK_CUR);

            cnt--;
          }
        }

        fseeko(outputfile, 0LL, SEEK_CUR);

        if(annotblock->duration[0]!=0)
        {
          fputc(21, outputfile);

          cnt++;

          cnt += fprintf(outputfile, "%s", annotblock->duration);
        }

        fputc(20, outputfile);

        cnt++;

        for(i = 0; i < (annot_signal_size - cnt - 2); i++)
        {
          if(annotblock->annotation[i]==0)  break;

          fputc(annotblock->annotation[i], outputfile);

          cnt++;
        }

        fputc(20, outputfile);

        cnt++;

        for(i = cnt; i < annot_signal_size; i++)
        {
          fputc(0, outputfile);
        }

        annots_written++;
      }

      annotblock = annotblock->next_annotation;
    }
  }

  free_annotations(annotationlist[0]);
  free(edfhdr->edfparam);
  free(edfhdr);
  free(fileheader);
  free(readbuf);
  fclose(inputfile);
  fclose(outputfile);

  progress.reset();
  textEdit1->append("Done\n");

  pushButton1->setEnabled(TRUE);
}



void UI_EDFDwindow::write_values_to_hdr(FILE *outputfile, long long timestamp, int datarecords, struct edfhdrblock *edfhdr)
{
  int i;

  struct date_time_struct date_time;

  utc_to_date_time(edfhdr->utc_starttime + (timestamp / TIME_DIMENSION), &date_time);

  fseeko(outputfile, 98LL, SEEK_SET);
  fprintf(outputfile, "%02i-%s-%04i",
          date_time.day,
          date_time.month_str,
          date_time.year);

  fseeko(outputfile, 168LL, SEEK_SET);
  fprintf(outputfile, "%02i.%02i.%02i%02i.%02i.%02i",
          date_time.day,
          date_time.month,
          date_time.year % 100,
          date_time.hour,
          date_time.minute,
          date_time.second);

  fseeko(outputfile, 192LL, SEEK_SET);
  if(edfhdr->edfplus)
  {
    fprintf(outputfile, "EDF+C");
  }
  else
  {
    fprintf(outputfile, "BDF+C");
  }

  for(i=0; i<39; i++)
  {
    fputc(' ', outputfile);
  }

  fseeko(outputfile, 236LL, SEEK_SET);
  fprintf(outputfile, "%-8i", datarecords);
}



long long UI_EDFDwindow::get_datarecord_timestamp(char *str)
{
  int i, len=8, hasdot=0, dotposition=0;

  long long value=0LL, radix;

  for(i=0; ; i++)
  {
    if(str[i]==20)
    {
      len = i;
      break;
    }
  }

  for(i=0; i<len; i++)
  {
    if(str[i]=='.')
    {
      hasdot = 1;
      dotposition = i;
      break;
    }
  }

  if(hasdot)
  {
    radix = TIME_DIMENSION;

    for(i=dotposition-1; i>=1; i--)
    {
        value += ((long long)(str[i] - 48)) * radix;
        radix *= 10;
    }

    radix = TIME_DIMENSION / 10;

    for(i=dotposition+1; i<len; i++)
    {
        value += ((long long)(str[i] - 48)) * radix;
        radix /= 10;
    }
  }
  else
  {
    radix = TIME_DIMENSION;

    for(i=len-1; i>=1; i--)
    {
        value += ((long long)(str[i] - 48)) * radix;
        radix *= 10;
    }
  }

  if(str[0]=='-')
  {
    value = -value;
  }

  return(value);
}



void UI_EDFDwindow::free_annotations(struct annotationblock *annot)
{
  if(annot==NULL)
  {
    return;
  }

  while(annot->next_annotation)
  {
    annot = annot->next_annotation;

    free(annot->former_annotation);
  }

  free(annot);
}







