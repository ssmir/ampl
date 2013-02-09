#!/usr/bin/env python
# This script uploads binaries from the build server to Google Code.

import datetime, os, shutil, sys, zipfile
from googlecode_upload import upload_find_auth

server = "callisto.local"
project = "ampl"

summaries = {
  "amplgsl": "AMPL bindings for the GNU Scientific Library",
  "ampltabl": "ODBC table handler",
  "gecode": "Gecode solver",
  "cbc": "COIN-OR CBC solver"
}

labels = {
  "linux32": "OpSys-Linux",
  "linux64": "OpSys-Linux",
  "macosx": "OpSys-OSX",
  "win32": "OpSys-Windows",
  "win64": "OpSys-Windows"
}

def upload(filename, summary, labels):
  print("Uploading {}".format(filename))
  status, reason, url = upload_find_auth(filename, project, summary, labels)
  os.remove(filename)
  if not url:
    print('Google Code upload error: {} ({})'.format(reason, status))
    sys.exit(1)

dir = "ampl-open"
for platform in reversed(["linux32", "linux64", "macosx", "win32", "win64"]):
  if os.path.exists(dir):
    shutil.rmtree(dir)
  print("Downloading binaries for {}:".format(platform))
  os.system("scp -r {}:/var/lib/buildbot/upload/{} {}".format(server, platform, dir))
  # Get versions.
  versions_filename = dir + "/versions"
  versions = {}
  if os.path.exists(versions_filename):
    with open(versions_filename) as f:
      for line in f:
        items = line.rstrip().split(' ')
        if len(items) < 2:
          continue
        versions[items[0].lower()] = items[1]
  date = datetime.datetime.today()
  date = "{}{:02}{:02}".format(date.year, date.month, date.day)
  dirlen = len(dir) + 1
  # Upload individual files.
  paths = []
  for base, dirs, files in os.walk(dir):
    for file in files:
      path = os.path.join(base, file)
      name = path[dirlen:]
      if name == "versions":
        continue
      basename = os.path.splitext(name)[0]
      suffix = versions.get(basename, date)
      archive_name = "{}-{}-{}.zip".format(basename, suffix, platform)
      with zipfile.ZipFile(archive_name, 'w', zipfile.ZIP_DEFLATED) as zip:
        zip.write(path, name)
      upload(archive_name, summaries[basename], [labels[platform]])
      paths.append(path)
  # Upload all in one archive.
  archive_name = "ampl-open-{}-{}.zip".format(date, platform)
  with zipfile.ZipFile(archive_name, 'w', zipfile.ZIP_DEFLATED) as zip:
    for path in paths:
      path = os.path.join(base, file)
      zip.write(path, path[dirlen:])
    zip.write("LICENSE", "LICENSE")
  upload(archive_name, "Open-source AMPL solvers and libraries", None)