/****************************************************************************************************************/
/*  Purpose:    Bouncy spectrogram!                                                                             */
/*  Author:     Copyright (c) 2019, W.B.Hill <mail@wbh.org> All rights reserved.                                */
/*  License:    The parts of this written by W.B.Hill are available, at your choice, under either:              */
/*                  The GPLv2 license                                                                           */
/*                Or                                                                                            */
/*                  The BSD2clause license                                                                      */
/*              See the included file LICENSE for full text.                                                    */
/****************************************************************************************************************/

// Standard stuff.
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
// For xy console.
#include <ncurses.h>
// Fourier transforms.
#include <fftw3.h>
// MultiThreading.
#include <pthread.h>
// Portable Sound IO.
#include <portaudio.h>


// Sample rate.
const int Fs = 8000 ;
// Buffer size.
const int N = 512 ;
// Number of buffers.
const int bc = 3 ;

// PSD indexes for G.711 300–3400Hz
// At Fs, max freq=Fs/2
// and FFT is N/2 long
// so (Fs/2)/(N/2) Hz per bucket
// Extras so bars on screen.
const int imin = (  300 * (N/2))/(Fs/2) -1 ;
const int imax = ( 3400 * (N/2))/(Fs/2) +2 ;

// About 3s AGC buffers.
const int alen = 3*Fs/N ;

// Are we active?
volatile bool running = true ;
// Which total?
volatile int an = 0 ;
// Which buffer?
volatile int bn = 0 ;
// Buffer totals.
float tot[alen] ;
// The recorded audio.
float buf[bc*N] ;


#ifdef DEBUG
// For basename()
#include <libgen.h>
FILE* logfile ;
#endif //DEBUG
// Print to the debug log.
int
lprintf ( char* msg, ... )
  {
  #ifdef DEBUG
  va_list args ;
  // Expand it out.
  va_start ( args, msg ) ;
  int ret = vfprintf ( logfile, msg, args ) ;
  fflush ( logfile ) ;
  va_end ( args ) ;
  return ret ;
  #else //DEBUG
  (void) msg ;
  return 0 ;
  #endif //DEBUG
  }


// Draw a pretty frame.
void
frame()
  {
  //CLS
  clear() ;
  // Top.
  attron ( COLOR_PAIR ( 2 ) ) ;
  move ( 0, 0 ) ;
  for ( int i=0; i<COLS; i++ ) addch ( '*' ) ;
  attron ( COLOR_PAIR ( 1 ) ) ;
  // Sides.
  for ( int j=1; j<LINES-3; j++ )
    {
    move ( j, 0 ) ;
    attron ( COLOR_PAIR ( 2 ) ) ;
    addch ( '*' ) ;
    attron ( COLOR_PAIR ( 1 ) ) ;
    for ( int i = 1 ; i < COLS-1 ; i++ ) addch ( ' ' ) ;
    attron ( COLOR_PAIR ( 2 ) ) ;
    addch ( '*' ) ;
    attron ( COLOR_PAIR ( 1 ) ) ;
    }
  // Base.
  move ( LINES-3, 0 ) ;
  attron ( COLOR_PAIR ( 2 ) ) ;
  for ( int i=0; i<COLS; i++ ) addch ( '*' ) ;
  attron ( COLOR_PAIR ( 1 ) ) ;
  // Status space.
  move ( LINES-2, 0 ) ;
  attron ( COLOR_PAIR ( 2 ) ) ;
  addch ( '*' ) ;
  attron ( COLOR_PAIR ( 1 ) ) ;
  // Ticks every 500Hz.
  const int fmin = 300 ;
  const int fmax = 3400 ;
  int old = fmin ;
  int width = COLS-2 ;
  float span = fmax-fmin ;
  for ( int i=1; i<COLS-1; i++ )
    {
    int cur = fmin + (i-1)*((span)/width) ;
    int rem = cur % 500 ;
    addch ( rem<old ? '+' : ' ' ) ;
    old = rem ;
    }
  attron ( COLOR_PAIR ( 2 ) ) ;
  addch ( '*' ) ;
  attron ( COLOR_PAIR ( 1 ) ) ;
  // Bottom.
  move ( LINES-1, 0 ) ;
  attron ( COLOR_PAIR ( 2 ) ) ;
  for ( int i=0;  i<COLS;  i++ ) addch ( '*' ) ;
  attron ( COLOR_PAIR ( 1 ) ) ;
  //Done.
  refresh() ;
  }


// Draw a bar of height 0<=dat<=1 at column pos.
void
drawbar ( int pos, float dat )
  {
  int len = LINES-4 ;
  int t1 = 6*len/10 ;
  int t2 = 8*len/10 ;
  // Clip
  dat = dat<0.0?0.0:dat ;
  dat = dat>1.0?1.0:dat ;
  // Bar length.
  int val = (int)(len*dat) ;
  // Blank.
  for ( int j=LINES-4; j>0; j-- )
    {
    move ( j, pos ) ;
    attron ( COLOR_PAIR ( 1 ) ) ;
    addch ( ' ' ) ;
    attron ( COLOR_PAIR ( 2 ) ) ;
    }
  // Draw.
  for ( int j=len; j>(len-val); j-- )
    {
    move ( j, pos ) ;
    int cp = 3 ;
    if ( (len-j)>t1 ) cp = 4 ;
    if ( (len-j)>t2 ) cp = 5 ;
    attron ( COLOR_PAIR ( cp ) ) ;
    addch ( '*' ) ;
    attron ( COLOR_PAIR ( 2 ) ) ;
    }
  }


// Called from PortAudio when there's data.
static int
callback
  (
  const void* ibuf,
  void* obuf,
  unsigned long fpb,
  const PaStreamCallbackTimeInfo* ti,
  PaStreamCallbackFlags sf,
  void* udat
  )
  {
  // Not playing back, so unused args.
  (void) obuf ;
  (void) fpb ;
  (void) ti ;
  (void) sf ;
  (void) udat ;
  const float* dat = ( const float* ) ibuf ;
  // Which buffer?
  float* rec = &buf[bn*N] ;
  // Process the buffer.
  tot[an] = 0.0 ;
  for ( int i=0; i<N; i++ )
    {
    // Store it.
    rec[i] = dat[i] ;
    // Save the total.
    float val = dat[i] ;
    tot[an] += val<0?-val:val ;
    }
  // Next buffer.
  bn = (bn+1) % bc ;
  an = (an+1) % alen ;
  // Keep going.
  return paContinue ;
  }


// Show the PSD.
void*
showpsd()
  {
  // The FFT.
  fftw_complex fft[N/2+1] ;
  // The Power Spectrum Density.
  float psd[N/2+1] ;
  float old[10][N/2+1] ;
  // The data.
  double dat[N] ;
  // Allocate the fftw structure.
  fftw_plan plan = fftw_plan_dft_r2c_1d ( N, dat, fft, FFTW_ESTIMATE ) ;
  // Loop until quit key pressed.
  while ( running )
    {
    // Screen width.
    int w=COLS-2;
    // PSD 'width'.
    int W=N/2+1 ;
    // Get the most recent buffer.
    float* rec = &buf[(bn-1)>-1?bn-1:bc-1] ;
    // AGC
    float agc = 0.0 ;
    for ( int i=0; i<alen; i++ ) agc += tot[i] ;
    agc /= ( an * N ) ;
    for ( int i=0; i<N; i++ ) dat[i] = (double) ( 2.5 * rec[i] / agc ) ;
    /*FIXME - zap this
    // Generate sample data.
    for ( int i=0; i<N; i++ )
      {
      dat[i]  = 1.0  * sin ( 2.0 * M_PI * i * 1850 / Fs ) ; //Center
      dat[i] += 1.0  * sin ( 2.0 * M_PI * i *  300 / Fs ) ; //Left
      dat[i] += 1.0  * sin ( 2.0 * M_PI * i * 3400 / Fs ) ; //Right
      dat[i] += 0.4  * sin ( 2.0 * M_PI * i * 1075 / Fs ) ; //0.25
      dat[i] += 0.5  * sin ( 2.0 * M_PI * i * 2625 / Fs ) ; //0.75
      } */
    // Do the FFT.
    fftw_execute ( plan ) ;
    // Compute the PSD.
    for ( int i=0; i<W; i++ ) psd[i] = 2.0 * ( fft[i][0] * fft[i][0] + fft[i][1] * fft[i][1] ) / N ;
//FIXME - 10*log10() ???
//    for ( int i=0; i<W; i++ ) psd[i] = (9.0+log10(psd[i]))/15.0 ;
    // Running smoother.
    for ( int i=0; i<W; i++ )
      {
      // Shuffle along.
      for ( int j=9; j>0; j-- ) old[j][i] = old[j-1][i] * 0.9 ;
      old[0][i] = psd[i] * 0.9 ;
      // Total up.
      float sum = psd[i] ;
      for ( int j=0; j<10; j++ ) sum += old[j][i] ;
      // 0.9^0+0.9^1+...+0.9^10
      psd[i] = sum / 6.8619 ;
      }
    // Each bar spans how many psd points?
    float s = (float)(imax-imin)/(float)w ;
    // 'K'urrent psd position.
    float k = imin ;
    // Previous, as an integer.
    int p = (int) k ;
    // Loop over all bars, with 'k'urrent psd index.
    for ( int i=1; i<=w; i++,k+=s )
      {
      // Current psd index.
      int c = (int) k ;
      // Bar value.
      float v = 0.0 ;
      // Go from previous to current psd index and find the peak.
      for ( int j=p; j<=c; j++ ) v = psd[j]>v?psd[j]:v ;
      // Update previous.
      p = c ;
      // Plot it.
      drawbar ( i, v/((float)N/2) ) ;
      }
    // Show it to the real screen.
    refresh() ;
    // Get busy doing nothing.
    //FIXME - tune value?
    usleep ( 40000 ) ;
    }
  // Free it up.
  fftw_destroy_plan ( plan ) ;
  // Done.
  pthread_exit ( NULL ) ;
  }


int
main ( int argc, char* argv[] )
  {
  #ifdef DEBUG
  char* logname ;
  asprintf ( &logname, "%s.log", basename ( argv[0] ) ) ;
  logfile = fopen ( logname, "a" ) ;
  free ( logname ) ;
  #endif //DEBUG
  // Anything gone wrong?
  int ret = EXIT_SUCCESS ;
  PaError err = paNoError ;
  char* msg = NULL ;
  // PortAudio parameters.
  PaStreamParameters param ;
  // PortAudio stream.
  PaStream* stream ;
  // Fire up PortAudio.
  err = Pa_Initialize() ;
  if ( err != paNoError ) goto finish ;
  // Get the default input device.
  param.device = Pa_GetDefaultInputDevice() ;
  // Check there is one!
  if ( param.device == paNoDevice )
    {
    ret = EXIT_FAILURE ;
    msg = "Error: No default recording device." ;
    goto finish ;
    }
  // Set mono, floating-point, defaults.
  param.channelCount = 1 ;
  param.sampleFormat = paFloat32 ;
  param.hostApiSpecificStreamInfo = NULL ;
  param.suggestedLatency = Pa_GetDeviceInfo ( param.device )->defaultLowInputLatency ;
  // Open the device.
  err = Pa_OpenStream ( &stream, &param, NULL, Fs, N, paClipOff, callback, NULL ) ;
  if ( err != paNoError ) goto finish ;
  // Start curses.
  initscr() ;
  // Colors.
  if ( has_colors() == FALSE )
    {
    ret = EXIT_FAILURE ;
    msg = "This terminal does not support color." ;
    goto finish ;
    }
  // Go colorful.
  start_color() ;
  init_pair ( 1, COLOR_WHITE,  COLOR_BLACK ) ;
  init_pair ( 2, COLOR_BLACK,  COLOR_WHITE ) ;
  init_pair ( 3, COLOR_GREEN,  COLOR_BLACK ) ;
  init_pair ( 4, COLOR_BLUE,   COLOR_BLACK ) ;
  init_pair ( 5, COLOR_RED,    COLOR_BLACK ) ;
  init_pair ( 6, COLOR_YELLOW, COLOR_BLUE ) ;
  // Line buffering disabled.
  raw() ;
  // We get F1, F2 etc.
  keypad ( stdscr, TRUE ) ;
  // Don't echo() while we do getch()
  noecho() ;
  // Hide the cursor.
  curs_set ( 0 ) ;
  // Draw the border.
  frame() ;
  // Setup the AGC.
  for ( int i=0; i<alen; i++ ) tot[i] = 0.0 ;
  // Start it doing stuff.
  if ( Pa_StartStream ( stream ) != paNoError ) goto finish ;
  // Start the display thread.
  pthread_t psdthread ;
  if ( pthread_create ( &psdthread, NULL, showpsd, NULL ) )
    {
    ret = EXIT_FAILURE ;
    msg = "Unable to start display thread." ;
    goto finish ;
    }
  // Main event loop.
  int ch ;
  while ( ( ch = getch() ) )
    {
    // Resize?
    if ( ch == KEY_RESIZE ) frame() ;
    // Quit?
    if ( ch == 'q' || ch == 'Q' || ch == 0x1B )
      {
      running = false ;
      break ;
      }
    }
  // Wait for the display to finish.
  pthread_join ( psdthread, NULL ) ;
  // Done now.
  err = Pa_CloseStream ( stream ) ;
  // Errors goto here.
  finish:
  // End curses mode.
  endwin() ;
  // Show PortAudio error messages.
  if ( err != paNoError )
    {
    ret = EXIT_FAILURE ;
    fprintf ( stderr, "PortAudio encountered an error.\n" ) ;
    fprintf ( stderr, "Error number: %d\n", err ) ;
    fprintf ( stderr, "Error message: %s\n", Pa_GetErrorText ( err ) ) ;
    }
  // Tidy up.
  Pa_Terminate() ;
  // Show any messages.
  if ( msg ) fprintf ( stderr, "%s\n", msg ) ;
  #ifdef DEBUG
  fclose ( logfile ) ;
  #endif //DEBUG
  // That's all, folks!!!
  return ret ;
  }
