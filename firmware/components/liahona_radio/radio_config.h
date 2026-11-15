/*
 * Radio settings shared by every ball and the base station. They must match on both ends.
 *
 * 915 MHz sits in the middle of the US licence-free band (902-928 MHz) and of the
 * JC Antenna whip's tuned range (905-920 MHz).
 * 500 kHz bandwidth keeps us in the "wideband digital" class for this band, which is
 * the cleanest fit for a fixed channel. SF10 gives roughly the same range as SF8 at
 * 125 kHz and keeps a ~20-byte packet under 100 ms on air.
 */
#pragma once

#define RADIO_FREQ_HZ       915000000UL
#define RADIO_BW_KHZ        500         /* 125, 250 or 500 */
#define RADIO_SF            10          /* 7..12: higher = longer range, slower */
#define RADIO_CR_DENOM      5           /* coding rate 4/5 */
#define RADIO_PREAMBLE      8
#define RADIO_SYNC_WORD     0x12        /* private network; Meshtastic and LoRaWAN use others */
