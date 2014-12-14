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

# Tapsync
printf 'M\x2ET\x01'

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

# Single-channel spots
printf 'M\x05V\x29'
printf 'M\x06V\x2a'
