#!/bin/bash

# Compress HTML
python3 scripts/compress_html.py html/home.html

# Update headers
python3 scripts/html_to_header.py html/home_compressed.html pico-constellation/include/ui/pages/home_page.h