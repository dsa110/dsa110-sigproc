#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* global variables describing the data */
#include "header.h"

/* list of subroutines and functions */
#include "sigproc.h"

#define PI 3.14159265
#define SPEED_OF_LIGHT 2.99792458e8
#define T_SUN 4.92544947


#define M_SUN 2.0e30
#define G 6.672e-11  



char inpfile[80], outfile[80];

FILE *output;


main (int argc, char *argv[])
{
  int i,c,j,k,s,bbm,swapout,smear,ic,arraysize,evenodd,headerless;
	float pulse,snr,min=-4.0,max=4.0;
	char string[80];
	double psrdm,faketime,prev_faketime,obstime,period,pulsephase,rising,trailing,dc,*shift, tpulse;
	double nexttime,timestep,tdm,p0,pdot,accn,speed_of_light=299792458.0,plst;
	long seed;
	float *fblock;
	float rwalk,rwsum;
	unsigned short *sblock;
	unsigned char *cblock;
	
	if (argc<2) {
		fake_help();
		exit(0);
	} else {
		print_version(argv[0],argv[1]);
	}

	/* set up default variables */
	strcpy(inpfile,"stdin");
	strcpy(outfile,"stdout");
	machine_id=telescope_id=0;
	machine_id=10;
	telescope_id=4;
	nchans=128;
	nbits=4;
	tstart=50000.0;
	tsamp=80.0e-6;
	fch1=433.968;
	foff=-0.062;
	smear=nifs=1;
	obstime=10.0;
	output=stdout;
	seed=-1;
	faketime=plst=0.0;
	psrdm=-1.0;
	period=-1.0;
	pdot=accn=0.0;
	pulse=0.0;
	snr=1.0;
	dc=0.04;
	evenodd=swapout=0;
	headerless=0;
	rwalk=0.0;
	rwsum=0;
	tpulse = 0.0;

	/* for multiple beams */
	int nbeam = 1;
	int nsblk = 512;
	
	/* parse the command line if specified */
	if (argc>1) {
		i=1;
		while (i<argc) {
			if (strings_equal(argv[i],"-nchans")) {
				i++;
				nchans=atoi(argv[i]);
			} else if (strings_equal(argv[i],"-nbeams")) {
			  i++;
			  nbeams = atoi(argv[i]);
			} else if (strings_equal(argv[i],"-nsblk")) {
			  i++;
			  nsblk = atoi(argv[i]);
			} else if (strings_equal(argv[i],"-period")) {
				i++;
				period=1.0e-3*atof(argv[i]);
			} else if (strings_equal(argv[i],"-pdot")) {
				i++;
				pdot=atof(argv[i]);
			} else if (strings_equal(argv[i],"-rednoise")) {
				i++;
				rwalk==atof(argv[i])*0.0005;
			} else if (strings_equal(argv[i],"-accn")) {
				i++;
				accn=atof(argv[i]);
			} else if (strings_equal(argv[i],"-snrpeak")) {
				i++;
				snr=atof(argv[i]);
			} else if (strings_equal(argv[i],"-dm")) {
				i++;
				psrdm=atof(argv[i]);
			} else if (strings_equal(argv[i],"-width")) {
				i++;
				dc=atof(argv[i])/1000.0;
			} else if (strings_equal(argv[i],"-tsamp")) {
				i++;
				tsamp=1.0e-6*atof(argv[i]);
			} else if (strings_equal(argv[i],"-tstart")) {
				i++;
				tstart=atof(argv[i]);
			} else if (strings_equal(argv[i],"-tobs")) {
				i++;
				obstime=atof(argv[i]);
			} else if (strings_equal(argv[i],"-nifs")) {
				i++;
				nifs=atoi(argv[i]);
			} else if (strings_equal(argv[i],"-nbits")) {
				i++;
				nbits=atoi(argv[i]);
			} else if (strings_equal(argv[i],"-fch1")) {
				i++;
				fch1=atof(argv[i]);
			} else if (strings_equal(argv[i],"-foff")) {
				i++;
				foff=atof(argv[i]);
			} else if (strings_equal(argv[i],"-seed")) {
				i++;
				seed=atol(argv[i]);
			} else if (strings_equal(argv[i],"-swapout")) {
				swapout=1;
			} else if (strings_equal(argv[i],"-nosmear")) {
				smear=0;
			} else if (strings_equal(argv[i],"-tpulse")) {
			  i++;
			  tpulse = atof(argv[i]);
			} else if (strings_equal(argv[i],"-evenodd")) {
				nbits=32;
				evenodd=1;
				smear=0;
				psrdm=0.0;
			} else if (strings_equal(argv[i],"-headerless")) {
				headerless=1;
			} else {
				/* unknown argument passed down - stop! */
				fake_help();
				sprintf(string,"unknown argument (%s) passed to fake.",argv[i]);
				error_message(string);
			}
			i++;
		}
	}

	/* get seed from ship's clock if not set */
	if (seed == -1) seed = startseed();

	/* get random period between 1 ms and 1 s if not set */
	if (period < 0.0) period=flat(1.0e-3,-period,&seed);

	/* get random DM between 1  and 1000 pc/cc if not set */
	if (psrdm < 0.0) psrdm=flat(1.0,1.0e3,&seed);

	/* make sure first channel is always highest frequency i.e. foff<0 */
	if (foff > 0.0) foff*=-1.0;

	if (smear && period > 0.0) {
		/* DM smearing time in seconds (small channel approximation) */
		tdm=8.3e3*psrdm*foff/fch1/fch1/fch1;
		dc=sqrt(tdm*tdm+tsamp*tsamp+dc*dc);
	}

	/* set pulse window if the data are correctly dedispersed then phase=0.5 */
	rising=0.5-dc/2.0;
	trailing=0.5+dc/2.0;

	/* get dm shift times */
	shift=(double *) malloc(sizeof(double)*nchans);
	for (c=0;c<nchans;c++) {
		shift[c]=dmdelay(fch1,fch1+((double)c*foff),psrdm);
	}

	/* scale single-pulse signal to noise by number of channels */
	snr/=sqrt((double) nchans);
	snr /= sqrt(dc/tsamp);
	if (snr > 1.0) max*=snr;

	rwalk/=sqrt((double) nchans);	

	/* define the data blocks */
	arraysize=nchans*nifs*nsblk;
	fblock=(float *) malloc(sizeof(float)*arraysize);
	sblock=(unsigned short *) malloc(sizeof(unsigned short)*arraysize);
	cblock=(unsigned char *) malloc(sizeof(unsigned char)*arraysize);


	/* open up logfile */
	open_log("fake.monitor");
	update_log("starting");

	if (!headerless) {
		/* broadcast header */
		if (evenodd) {
			strcpy(source_name,"Even-Odd channel test");
		} else {
			sprintf(source_name,"P: %.12f ms, DM: %.3f",period*1000.0,psrdm);
		}
		send_string("HEADER_START");
		send_string("source_name");
		send_string(source_name);
		send_int("machine_id",machine_id);
		send_int("telescope_id",telescope_id);
		//send_int("nbeams",nbeams);
		//send_int("nsblk",nsblk);
		if (nchans > 1) { 
			send_int("data_type",1);
		} else {
			send_int("data_type",2); 
			send_double("refdm",psrdm);
		}
		send_double("fch1",fch1);
		send_double("foff",foff);
		send_int("nchans",nchans);
		send_int("nbits",nbits);
		send_double("tstart",tstart);
		send_double("tsamp",tsamp);
		send_int("nifs",nifs);
		send_string("HEADER_END");
	}
	nexttime=timestep=1.0;
	ic=nifs*nchans;

	p0=period;
	if (accn != 0.0) p0=period/(1.0+accn*obstime/2.0/speed_of_light);
	period=p0;

	/* main loop */
	do  {
	  prev_faketime=faketime;
	  for (bbm=0;bbm<nbeams;bbm++) {
	    faketime=prev_faketime;
	    for (s=0;s<nsblk;s++) {
			faketime+=tsamp;
			if (rwalk != 0.0){
				if (gasdev(&seed) > 2.8)rwalk=-rwalk;
				if (rwsum >= max/2)rwalk=-fabs(rwalk);
				if (rwsum <= min/2)rwalk=fabs(rwalk);
				rwsum+=rwalk;
			}
			for (i=0;i<nifs;i++) {
				for (c=0;c<nchans;c++) {
					if (evenodd) {
						fblock[s*ic+i*nchans+c]=(float)(c%2);
					}
					else {

					  if (fabs((faketime+shift[c])-tpulse)<=dc/2.) {
					    pulse = snr;
					  }
					  else pulse = 0.0;
					  
						if (period > 0.0) {
							pulsephase=(faketime+shift[c])/period;
							pulsephase=pulsephase-floor(pulsephase);
							if ( (pulsephase>=rising) && (pulsephase<=trailing) ) {
								pulse=snr;
							} else {
								pulse=0.0;
							}
							if (plst>pulsephase) {
								if (pdot != 0.0) period=p0+pdot*faketime;
								if (accn != 0.0) period=p0*(1.0+accn*faketime/speed_of_light);
							}
							plst=pulsephase;
						}
						fblock[s*ic+i*nchans+c]=gasdev(&seed)+pulse +rwsum;
					}
				}
			}
		}
		/* write out a block */
		switch (nbits) {
			case 32:
				if (swapout) for (i=0; i<arraysize; i++) swap_float(&fblock[i]);
				fwrite(fblock,sizeof(float),arraysize,output);
				break;
			case 16:
				float2short(fblock,arraysize,min,max,sblock);
				if (swapout) for (i=0; i<arraysize; i++) swap_short(&sblock[i]);
				fwrite(sblock,sizeof(unsigned short),arraysize,output);
				break;
			case 8:
				float2char(fblock,arraysize,min,max,cblock);
				fwrite(cblock,sizeof(unsigned char),arraysize,output);
				break;
			case 4:
				float2four(fblock,arraysize,min,max,cblock);
				fwrite(cblock,sizeof(unsigned char),arraysize/2,output);
				break;
			case 2:
				float2two(fblock,arraysize,min,max,cblock);
				fwrite(cblock,sizeof(unsigned char),arraysize/4,output);
				break;

			case 1:
				float2one(fblock,arraysize,min,max,cblock);
				fwrite(cblock,sizeof(unsigned char),arraysize/8,output);
				break;
			default:
				sprintf(string,"fake cannot quantize data to %d bits",nbits);
				error_message(string);
				break;
		}
	  }
	} while (faketime < obstime);

	/* all done, update log, close all files and exit normally */
	update_log("finished");
	close_log();
	fclose(output);  
	free(fblock);free(cblock);free(sblock);
	exit(0);
}



























	

