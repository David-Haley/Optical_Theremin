// This file implements the I2S audio output. Two DMA channels are
// chained so that each triggers the other on completion, each
// transferring its own buffer of frames to the PIO TX FIFO. When a
// channel completes, its interrupt refills that buffer from
// DDS_Generator while the other channel is transferring.
// Author : David Haley

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/structs/io_bank0.h"

#include "audio_i2s.pio.h"
#include "dds_generator.h"
#include "i2s_output.h"

_Static_assert (I2S_LRCK_Pin == I2S_BCK_Pin + 1,
                "The PIO program requires LRCK to be BCK + 1");

// Frames per buffer, 128 frames is approximately 2.9 ms. Each frame is
// two 32 bit words, left then right.
#define Buffer_Frames 128
#define Buffer_Words (Buffer_Frames * 2)

static PIO const Audio_Pio = pio0;
static uint State_Machine;
static int DMA_Channel [2];
static uint32_t Buffer [2] [Buffer_Words];

// Written by the main loop, read by the DMA interrupt. A 32 bit
// store is atomic so no further protection is required.
static volatile int Distance = Lowest_Note_MM;
static volatile Waveforms Waveform = Sine;
static volatile int Volume_Distance = Mute_MM;

// Fills a buffer with frames. Each frame is the left sample followed
// by the right sample, see audio_i2s.pio.
static void __time_critical_func (Fill_Buffer) (uint32_t *Words) {
  const int Current_Distance = Distance;
  const Waveforms Current_Waveform = Waveform;
  const int Current_Volume_Distance = Volume_Distance;
  Audio_Sample Sample;

  for (int F = 0; F < Buffer_Frames; F++) {
    Sample = DDS_Generator (Current_Waveform, Current_Distance,
                            Current_Volume_Distance);
    Words [2 * F] = (uint32_t) Sample.Left;
    Words [2 * F + 1] = (uint32_t) Sample.Right;
  } // F < Buffer_Frames
} // Fill_Buffer

static void __time_critical_func (DMA_Handler) (void) {
  for (int B = 0; B < 2; B++) {
    if (dma_channel_get_irq0_status (DMA_Channel [B])) {
      dma_channel_acknowledge_irq0 (DMA_Channel [B]);
      Fill_Buffer (Buffer [B]);
      // Rearm without triggering, the other channel's chain_to will
      // trigger this channel when it completes.
      dma_channel_set_read_addr (DMA_Channel [B], Buffer [B], false);
    } // dma_channel_get_irq0_status (DMA_Channel [B])
  } // B < 2
} // DMA_Handler

void I2S_Output_Start (void) {
  uint Offset = pio_add_program (Audio_Pio, &audio_i2s_program);
  dma_channel_config Config;

  State_Machine = pio_claim_unused_sm (Audio_Pio, true);
  audio_i2s_program_init (Audio_Pio, State_Machine, Offset, I2S_DIN_Pin,
                          I2S_BCK_Pin, PIO_Clock_Divisor);
  for (int B = 0; B < 2; B++) {
    DMA_Channel [B] = dma_claim_unused_channel (true);
  } // B < 2
  for (int B = 0; B < 2; B++) {
    Fill_Buffer (Buffer [B]);
    Config = dma_channel_get_default_config (DMA_Channel [B]);
    channel_config_set_transfer_data_size (&Config, DMA_SIZE_32);
    channel_config_set_read_increment (&Config, true);
    channel_config_set_write_increment (&Config, false);
    channel_config_set_dreq (&Config,
                             pio_get_dreq (Audio_Pio, State_Machine, true));
    channel_config_set_chain_to (&Config, DMA_Channel [1 - B]);
    dma_channel_configure (DMA_Channel [B], &Config,
                           &Audio_Pio->txf [State_Machine], Buffer [B],
                           Buffer_Words, false);
    dma_channel_set_irq0_enabled (DMA_Channel [B], true);
  } // B < 2
  irq_set_exclusive_handler (DMA_IRQ_0, DMA_Handler);
  irq_set_enabled (DMA_IRQ_0, true);
  // Start the DMA first so that the TX FIFO is full when the state
  // machine starts.
  dma_channel_start (DMA_Channel [0]);
  pio_sm_set_enabled (Audio_Pio, State_Machine, true);
} // I2S_Output_Start

// Samples the pad status of a pin for 1 ms and counts transitions of
// the level the peripheral drives out to the pad and of the level read
// back from the pad. Driven transitions with no read back transitions
// mean something external is overriding the pin.
static void Report_Pad (const char *Name, const uint Pin) {
  absolute_time_t End = make_timeout_time_ms (1);
  uint32_t Status = io_bank0_hw->io [Pin].status;
  uint32_t Last_Out = Status & IO_BANK0_GPIO0_STATUS_OUTTOPAD_BITS;
  uint32_t Last_In = Status & IO_BANK0_GPIO0_STATUS_INFROMPAD_BITS;
  uint Out_Transitions = 0, In_Transitions = 0;
  bool Output_Enabled = (Status & IO_BANK0_GPIO0_STATUS_OETOPAD_BITS) != 0;

  while (!time_reached (End)) {
    Status = io_bank0_hw->io [Pin].status;
    if ((Status & IO_BANK0_GPIO0_STATUS_OUTTOPAD_BITS) != Last_Out) {
      Last_Out ^= IO_BANK0_GPIO0_STATUS_OUTTOPAD_BITS;
      Out_Transitions++;
    } // Out to pad changed
    if ((Status & IO_BANK0_GPIO0_STATUS_INFROMPAD_BITS) != Last_In) {
      Last_In ^= IO_BANK0_GPIO0_STATUS_INFROMPAD_BITS;
      In_Transitions++;
    } // In from pad changed
  } // !time_reached (End)
  printf ("I2S pad %s (GP%u): function = %lu, output enabled = %d, "
          "driven transitions = %u, read back transitions = %u\n", Name,
          Pin, (unsigned long) (io_bank0_hw->io [Pin].ctrl &
                                IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS),
          Output_Enabled, Out_Transitions, In_Transitions);
} // Report_Pad

void I2S_Output_Report_Status (void) {
  const uint32_t Stall_Mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + State_Machine);
  const bool Stalled = (Audio_Pio->fdebug & Stall_Mask) != 0;

  Audio_Pio->fdebug = Stall_Mask; // write 1 to clear
  printf ("I2S status: SM %u enabled = %d, TX FIFO level = %u, "
          "TX stall seen = %d, DMA busy = %d %d\n", State_Machine,
          (int) ((Audio_Pio->ctrl >> State_Machine) & 1u),
          pio_sm_get_tx_fifo_level (Audio_Pio, State_Machine), Stalled,
          dma_channel_is_busy (DMA_Channel [0]),
          dma_channel_is_busy (DMA_Channel [1]));
  Report_Pad ("BCK", I2S_BCK_Pin);
  Report_Pad ("LRCK", I2S_LRCK_Pin);
  Report_Pad ("DIN", I2S_DIN_Pin);
} // I2S_Output_Report_Status

void I2S_Output_Set_Distance (int New_Distance) {
  Distance = New_Distance;
} // I2S_Output_Set_Distance

void I2S_Output_Set_Waveform (Waveforms New_Waveform) {
  Waveform = New_Waveform;
} // I2S_Output_Set_Waveform

void I2S_Output_Set_Volume (int New_Volume_Distance) {
  Volume_Distance = New_Volume_Distance;
} // I2S_Output_Set_Volume
