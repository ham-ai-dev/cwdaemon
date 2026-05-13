#!/usr/bin/env python3
"""Fetch W1AW propagation bulletin text from ARRL and save as ground truth.

Usage:
    ./scripts/fetch_w1aw.py                  # Fetch latest bulletin
    ./scripts/fetch_w1aw.py --date 2026-05-08  # Fetch specific date
    ./scripts/fetch_w1aw.py --list           # List available bulletins
"""

import argparse
import re
import sys
import json
import os
from urllib.request import urlopen
from html.parser import HTMLParser
from datetime import datetime


ARCHIVE_URL = "https://www.arrl.org/w1aw-bulletins-archive-propagation"
ISSUE_URL = "https://www.arrl.org/w1awbulletinspropagationissue?issue={date}&code={code}"


class SimpleHTMLTextExtractor(HTMLParser):
    """Extract text content from HTML."""
    def __init__(self):
        super().__init__()
        self.text = []
        self._skip = False

    def handle_starttag(self, tag, attrs):
        if tag in ('script', 'style', 'head'):
            self._skip = True

    def handle_endtag(self, tag):
        if tag in ('script', 'style', 'head'):
            self._skip = False
        if tag in ('p', 'br', 'div', 'h1', 'h2', 'h3', 'h4'):
            self.text.append('\n')

    def handle_data(self, data):
        if not self._skip:
            self.text.append(data)

    def get_text(self):
        return ''.join(self.text)


def fetch_page(url):
    """Fetch URL and return text content."""
    with urlopen(url) as resp:
        html = resp.read().decode('utf-8', errors='replace')
    parser = SimpleHTMLTextExtractor()
    parser.feed(html)
    return parser.get_text()


def list_bulletins():
    """List available W1AW propagation bulletins."""
    text = fetch_page(ARCHIVE_URL)
    # Extract dates and codes from the archive page
    pattern = r'((?:January|February|March|April|May|June|July|August|September|October|November|December)\s+\d{2},\s+\d{4})\s+(ARLP\d+)'
    matches = re.findall(pattern, text)
    for date_str, code in matches[:20]:  # Show last 20
        print(f"  {date_str:30s} {code}")
    return matches


def fetch_bulletin(date_str, code):
    """Fetch a specific bulletin and extract the body text."""
    url = ISSUE_URL.format(date=date_str, code=code)
    text = fetch_page(url)

    # Extract the bulletin body — everything between "QST de W1AW" and "NNNN"
    match = re.search(r'QST de W1AW\s*\n(.+?)NNNN', text, re.DOTALL)
    if not match:
        # Try broader pattern
        match = re.search(r'ARLP\d+ The ARRL Solar Report\s*\n(.+?)NNNN', text, re.DOTALL)
    if not match:
        print(f"ERROR: Could not extract bulletin body from {url}", file=sys.stderr)
        return None

    body = match.group(1).strip()

    # Clean up: normalize whitespace, remove URLs, remove line breaks within paragraphs
    body = re.sub(r'https?://\S+', '', body)
    body = re.sub(r'\[.*?\]', '', body)
    body = re.sub(r'\s+', ' ', body)
    body = body.strip()

    return body


def find_latest_bulletin():
    """Find the most recent bulletin date and code."""
    text = fetch_page(ARCHIVE_URL)
    # Match the issue URL pattern
    pattern = r'issue=(\d{4}-\d{2}-\d{2})&(?:amp;)?code=(ARLP\d+)'
    matches = re.findall(pattern, text)
    if not matches:
        print("ERROR: Could not find any bulletins", file=sys.stderr)
        return None, None
    return matches[0]  # First match is the most recent


def main():
    parser = argparse.ArgumentParser(description='Fetch W1AW propagation bulletins')
    parser.add_argument('--date', help='Bulletin date (YYYY-MM-DD)')
    parser.add_argument('--code', help='Bulletin code (e.g. ARLP019)')
    parser.add_argument('--list', action='store_true', help='List available bulletins')
    parser.add_argument('--output', help='Output file path')
    parser.add_argument('--upper', action='store_true', help='Convert to uppercase (CW format)')
    args = parser.parse_args()

    if args.list:
        print("Available W1AW Propagation Bulletins:")
        list_bulletins()
        return

    if args.date and args.code:
        date_str = args.date
        code = args.code
    elif args.date:
        # Try to guess code from date
        code = f"ARLP{int(args.date.split('-')[1]):03d}"  # rough guess
        date_str = args.date
    else:
        date_str, code = find_latest_bulletin()
        if not date_str:
            sys.exit(1)
        print(f"Latest bulletin: {date_str} {code}", file=sys.stderr)

    body = fetch_bulletin(date_str, code)
    if not body:
        sys.exit(1)

    if args.upper:
        body = body.upper()

    if args.output:
        os.makedirs(os.path.dirname(args.output) or '.', exist_ok=True)
        with open(args.output, 'w') as f:
            f.write(body + '\n')
        print(f"Saved to {args.output}", file=sys.stderr)
    else:
        print(body)


if __name__ == '__main__':
    main()
