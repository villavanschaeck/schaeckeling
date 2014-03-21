#!/usr/bin/env bash

# Reset all inputs
printf 'R'

# Master
printf 'M\x07M'

# BPM
printf 'M\x17B'

# RUN
printf 'M\x29S'

# Blackout
printf 'M\x2DD'

# LED-par 1
printf 'M\x002\x10\x01'

# LED-par 2
printf 'M\x012\x11\x09'

# LED-par 3
printf 'M\x022\x12\x11'

# LED-par 4
printf 'M\x032\x13\x19'

# LED-par 5
printf 'M\x042\x14\x21'

# LED-par 6
printf 'M\x052\x15\x29'

# LED-par 7
printf 'M\x062\x16\x31'
