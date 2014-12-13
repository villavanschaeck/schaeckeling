#!/usr/bin/env bash

# Reset all inputs
printf 'R'

# Master
printf 'M\x1BM'

# Chase
printf 'M\x1AP'

# BPM
printf 'M\x18B'

# RUN
#printf 'M\x29S'

# Blackout
printf 'M\x30D'

# LED-par 1
printf 'M\x002\x0C\x01'

# LED-par 2
printf 'M\x012\x0D\x09'

# LED-par 3
printf 'M\x022\x0E\x11'

# LED-par 4
printf 'M\x032\x0F\x19'

# LED-par 5
printf 'M\x042\x10\x21'

# LED-par 6
printf 'M\x052\x11\x29'

# LED-par 7
printf 'M\x062\x12\x31'

# LED-par 8
printf 'M\x072\x13\x39'

# LED-par 9
printf 'M\x082\x14\x41'

# LED-par 10
printf 'M\x092\x15\x49'

# LED-par 11
printf 'M\x0A2\x16\x51'

# LED-par 12
printf 'M\x0B2\x17\x59'

# Stroboscoop
printf 'M\x19V\x61' # X-fade: strobe frequency
printf 'V\x62\xff' # strobe intensity vast op 255

# Laser
printf 'M\x1fL\x65' # Eerste kanaal van de laser

# Rookmachine
printf 'M\x20V\x70'

# volgende mag op 0x71
