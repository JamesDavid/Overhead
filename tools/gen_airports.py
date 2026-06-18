#!/usr/bin/env python3
# Generate src/assets/Airports.h - a compact US airport + radio-frequency table for
# the Aircraft tab's "nearest airport + likely frequencies" feature.
#
# SOURCE: OurAirports (https://ourairports.com), public-domain, aggregates the FAA's
# NASR data for US fields. Two CSVs: airports.csv + airport-frequencies.csv.
# To refresh: re-run this script (it re-downloads the CSVs), then push the file to the
# device WITHOUT wiping LittleFS (settings/touch-cal survive):
#   python tools/gen_airports.py data/airports.bin
#   curl -H "Content-Type: application/octet-stream" --data-binary @data/airports.bin \
#        "http://<device>/api/fs?path=/airports.bin"   # then reboot
# For the authoritative FAA 28-day cycle instead, swap in the NASR APT_BASE.csv /
# FRQ.csv and keep the same packed layout below.
#
# Layout: kAirports[] = {id[4] (space-padded ident), int16 lat*100/lon*100 (0.01 deg ~
# 0.6 nm), uint16 freq-offset, uint8 freq-count}. Each airport's frequencies live in
# parallel kFreqType[] (type code) + kFreq40[] (MHz*40, 25 kHz steps) arrays. Up to 10
# freqs/airport, kept by type priority. Codes index TYPE_LABELS (mirrored on-device).
import csv, os, sys, struct, urllib.request

TMP = os.environ.get('TEMP') or os.environ.get('TMP') or '/tmp'
BASE = 'https://davidmegginson.github.io/ourairports-data/'

def fetch(local, remote):
    path = os.path.join(TMP, local)
    if not os.path.exists(path) or os.path.getsize(path) < 1000:
        print('downloading', remote, file=sys.stderr)
        urllib.request.urlretrieve(BASE + remote, path)
    return path

# type code (priority order) + the label shown on the device. OurAirports types map in.
TYPE_LABELS = ['TWR','GND','ATIS','CLR','CTAF','UNI','APP','DEP','A/D','AWOS',
               'RDO','CTR','AFIS','FSS','ATF','MISC']
TYPE_MAP = {'TWR':0,'GND':1,'ATIS':2,'CLD':3,'CTAF':4,'UNIC':5,'APP':6,'DEP':7,
            'A/D':8,'AWOS':9,'ASOS':9,'RDO':10,'CNTR':11,'AFIS':12,'FSS':13,'ATF':14}
def code(t): return TYPE_MAP.get(t, 15)   # everything else -> MISC

CAP = 10
ap_csv = fetch('airports.csv', 'airports.csv')
fq_csv = fetch('freqs.csv', 'airport-frequencies.csv')

byapt = {}
for r in csv.DictReader(open(fq_csv, encoding='utf-8')):
    try: f = float(r['frequency_mhz'])
    except ValueError: continue
    if not (108.0 <= f <= 137.0): continue
    byapt.setdefault(r['airport_ident'], []).append((code(r['type']), int(round(f * 40))))

WANT = {'large_airport', 'medium_airport', 'small_airport'}
airports, ftype, f40 = [], [], []
for a in csv.DictReader(open(ap_csv, encoding='utf-8')):
    if a['iso_country'] != 'US' or a['type'] not in WANT: continue
    ident = a['ident']
    if len(ident) > 4: continue
    fl = byapt.get(ident)
    if not fl: continue
    # keep by type priority (code asc), dedupe identical (type,freq), cap
    seen, kept = set(), []
    for c, v in sorted(fl, key=lambda x: x[0]):
        if (c, v) in seen: continue
        seen.add((c, v)); kept.append((c, v))
        if len(kept) >= CAP: break
    try:
        lat = int(round(float(a['latitude_deg']) * 100)); lon = int(round(float(a['longitude_deg']) * 100))
    except ValueError: continue
    if not (-9000 <= lat <= 9000 and -18000 <= lon <= 18000): continue
    off = len(ftype)
    for c, v in kept: ftype.append(c); f40.append(v)
    airports.append((ident.ljust(4), lat, lon, off, len(kept)))

airports.sort(key=lambda r: r[0])
# offsets were assigned before the sort, so they still point at the right blob slices.

# Binary file for LittleFS (little-endian, packed - mirrored by services/AirportDB.cpp):
#   "AP01", uint32 nAirports, uint32 nFreqs,
#   nAirports * { char id[4]; int16 lat100; int16 lon100; uint16 fOff; uint8 fCnt }  (11 B)
#   nFreqs   * uint8  freq type code
#   nFreqs   * uint16 freq MHz*40
blob = bytearray()
blob += struct.pack('<4sII', b'AP01', len(airports), len(ftype))
for id4, lat, lon, off, cnt in airports:
    blob += struct.pack('<4shhHB', id4.encode('ascii'), lat, lon, off, cnt)
blob += bytes(ftype)
blob += struct.pack('<%dH' % len(f40), *f40)
open(sys.argv[1], 'wb').write(blob)
print(f'{len(airports)} airports, {len(ftype)} freqs  -> {sys.argv[1]}  {len(blob)} bytes', file=sys.stderr)
print('freq type labels (mirror in AirportDB.cpp):', TYPE_LABELS, file=sys.stderr)
