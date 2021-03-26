# Copyright 2021 Joe Drago. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# READ THIS WHOLE COMMENT FIRST, BEFORE RUNNING THIS SCRIPT:

# AVIFs created with very old copies of avifenc (versions prior to v0.7.2) did not correctly set the
# "essential" flag on av1C item property associations. This is likely to cause future AVIF decoders
# (including libavif/avifdec!) to refuse to parse them. Luckily, this is an easy thing to adjust
# in-place in an affected AVIF, and does not change the file's size (it just toggles a bit or two).

# The goal of this script is to detect AVIFs containing item property associations that are not
# flagged as "essential" but should be, and fix those essential flags in-place by re-writing the
# file. The syntax is simple:

#     coffee fix_essential.coffee filename.avif

# This will look over the associations and if it detects an incorrect essential flag, it will fix it
# in memory, make a adjacent backup of the file (filename.avif.essentialBackup), and then overwrite
# the original file with the fixed contents. Using -v on the commandline will enable Verbose mode,
# and using -n will disable the creation of backups (.essentialBackup files).

# This should be well-behaved on files created by avifenc prior to version v0.7.2 (when these
# erroneous bits could be set), but **PLEASE** make backups of your images before running this
# script on them, **especially** if you plan to run with "-n".

# Possible responses for a file:
# * [NotAvif] This file isn't an AVIF.
# * [BadAvif] This file thinks it is an AVIF, but is missing important things.
# * [Skipped] This file is an AVIF, but didn't need any fixes.
# * [Success] This file is an AVIF, had to be fixed, and was fixed.
# * (the script crashes) I probably have a bug; let me know.

# -------------------------------------------------------------------------------------------------
# Syntax

syntax = ->
  console.log "Syntax: fix_essential [-v] [-n] file1 [file2 ...]"
  console.log "        -v : Verbose mode"
  console.log "        -n : No Backups (Don't generate adjacent .essentialBackup files when overwriting in-place)"

# -------------------------------------------------------------------------------------------------
# Constants and helpers

fs = require 'fs'

INDENT = "         "
VERBOSE = false

verboseLog = ->
  if VERBOSE
    console.log.apply(null, arguments)

fatalError = (reason) ->
  console.error "ERROR: #{reason}"
  process.exit(1)

# -------------------------------------------------------------------------------------------------
# Box

class Box
  constructor: (@filename, @type, @buffer, @start, @size) ->
    @offset = @start
    @bytesLeft = @size
    @version = 0
    @flags = 0
    @boxes = {} # child boxes

  nextBox: ->
    if @bytesLeft < 8
      return null
    boxSize = @buffer.readUInt32BE(@offset)
    boxType = @buffer.toString('utf8', @offset + 4, @offset + 8)
    if boxSize > @bytesLeft
      verboseLog("#{INDENT} * Truncated box of type #{boxType} (#{boxSize} bytes with only #{@bytesLeft} bytes left)")
      return null
    if boxSize < 8
      verboseLog("#{INDENT} * Bad box size of type #{boxType} (#{boxSize} bytes")
      return null
    newBox = new Box(@filename, boxType, @buffer, @offset + 8, boxSize - 8)
    @offset += boxSize
    @bytesLeft -= boxSize
    return newBox

  walkBoxes: ->
    while box = @nextBox()
      @boxes[box.type] = box
      verboseLog "#{INDENT} * Discovered box type: #{box.type} offset: #{box.offset - 8} size: #{box.size + 8}"
    return

  readFullBoxHeader: ->
    if @bytesLeft < 4
      fatalError("#{INDENT} * Truncated FullBox of type #{boxType} (only #{@bytesLeft} bytes left)")
    versionAndFlags = @buffer.readUInt32BE(@offset)
    @version = (versionAndFlags >> 24) & 0xFF
    @flags = versionAndFlags & 0xFFFFFF
    @offset += 4
    @bytesLeft -= 4
    return

  # Replaces the most recently read U8 with a new value
  fixU8: (newValue) ->
    if @offset < 1
      fatalError("#{INDENT} * impossible call to fixU8!")
    @buffer.writeUInt8(newValue, @offset - 1)

  readU8: ->
    if @bytesLeft < 1
      fatalError("#{INDENT} * Truncated read of U8 from box of type #{boxType} (only #{@bytesLeft} bytes left)")
    ret = @buffer.readUInt8(@offset)
    @offset += 1
    @bytesLeft -= 1
    return ret

  readU16: ->
    if @bytesLeft < 2
      fatalError("#{INDENT} * Truncated read of U16 from box of type #{boxType} (only #{@bytesLeft} bytes left)")
    ret = @buffer.readUInt16BE(@offset)
    @offset += 2
    @bytesLeft -= 2
    return ret

  readU32: ->
    if @bytesLeft < 4
      fatalError("#{INDENT} * Truncated read of U32 from box of type #{boxType} (only #{@bytesLeft} bytes left)")
    ret = @buffer.readUInt32BE(@offset)
    @offset += 4
    @bytesLeft -= 4
    return ret

  ftypHasBrand: (brand) ->
    if @type != 'ftyp'
      fatalError("#{INDENT} * Calling Box.ftypHasBrand() on a non-ftyp box")
    majorBrand = @buffer.toString('utf8', @offset, @offset + 4)
    compatibleBrands = []
    compatibleBrandCount = Math.floor((@size - 8) / 4)
    for i in [0...compatibleBrandCount]
      o = @offset + 8 + (i * 4)
      compatibleBrand = @buffer.toString('utf8', o, o + 4)
      compatibleBrands.push compatibleBrand

    verboseLog "#{INDENT}   * ftyp majorBrand: #{majorBrand} compatibleBrands: [#{compatibleBrands.join(', ')}]"

    if majorBrand == brand
      return true
    for compatibleBrand in compatibleBrands
      if compatibleBrand == brand
        return true
    return false

# -------------------------------------------------------------------------------------------------
# Main

fixEssential = (filename, makeBackups) ->
  if not fs.existsSync(filename)
    fatalError("File doesn't exist: #{filename}")
  try
    fileBuffer = fs.readFileSync(filename)
  catch e
    fatalError "Failed to read \"#{filename}\": #{e}"

  fileBox = new Box(filename, "<file>", fileBuffer, 0, fileBuffer.length)
  fileBox.walkBoxes()

  ftypBox = fileBox.boxes.ftyp
  if not ftypBox?
    return "NotAvif"
  if ftypBox.type != 'ftyp'
    return "NotAvif"
  if !ftypBox.ftypHasBrand('avif')
    return "NotAvif"

  metaBox = fileBox.boxes.meta
  if not metaBox?
    return "BadAvif"
  metaBox.readFullBoxHeader()
  metaBox.walkBoxes()

  iprpBox = metaBox.boxes.iprp
  if not iprpBox?
    return "BadAvif"

  ipcoBox = null
  ipmaBoxes = []
  while box = iprpBox.nextBox()
    if box.type == 'ipco'
      if ipcoBox?
        fatalError("#{INDENT} * Multiple ipco boxes found in a single ipma box!")
      ipcoBox = box
    else if box.type == 'ipma'
      ipmaBoxes.push box
  if not ipcoBox? or (ipmaBoxes.length == 0)
    return "BadAvif"

  properties = {}
  propertyIndex = 0
  while box = ipcoBox.nextBox()
    propertyIndex += 1
    properties[propertyIndex] =
      type: box.type
      essential: false
    switch box.type
      when 'av1C', 'lsel', 'clap', 'irot', 'imir'
        properties[propertyIndex].essential = true

  fixedBit = false
  for ipmaBox in ipmaBoxes
    ipmaBox.readFullBoxHeader()
    ipmaEntryCount = ipmaBox.readU32()
    for ipmaEntryIndex in [0...ipmaEntryCount]
      if ipmaBox.version < 1
        itemID = ipmaBox.readU16()
      else
        itemID = ipmaBox.readU32()
      associationCount = ipmaBox.readU8()
      verboseLog "#{INDENT}   * Item ID #{itemID} has #{associationCount} associations"
      for associationIndex in [0...associationCount]
        if ipmaBox.flags & 0x1
          essentialAndIndex = ipmaBox.readU16()
          essentialBit = ((essentialAndIndex & 0x8000) != 0)
          index = essentialAndIndex & 0x7FFF
        else
          essentialAndIndex = ipmaBox.readU8()
          essentialBit = ((essentialAndIndex & 0x80) != 0)
          index = essentialAndIndex & 0x7F
        if not properties[index]?
          fatalError("#{INDENT}     * Impossible property index #{index}")
        if properties[index].essential
          if essentialBit == 0
            state = "Bad"
          else
            state = "Good"
        else
          state = "OK"
        verboseLog "#{INDENT}     * #{associationIndex} -> index: #{index} (#{properties[index].type}), #{if essentialBit > 0 then "essential" else "non-essential"} [#{state}]"
        if not essentialBit and properties[index].essential
          verboseLog "#{INDENT}       * Fixing index #{index}"
          fixedBit = true
          fixedEssentialAndIndex = index | 0x80
          ipmaBox.fixU8(fixedEssentialAndIndex)

  if fixedBit
    if makeBackups
      backupFilename = filename + ".essentialBackup"
      fs.writeFileSync(backupFilename, fs.readFileSync(filename))
    fs.writeFileSync(filename, fileBuffer)
    return "Success"
  return "Skipped"

main = ->
  showSyntax = false
  makeBackups = true
  files = []

  for arg in process.argv.slice(2)
    switch arg
      when '-h', '--help'
        showSyntax = true
        break
      when '-n', '--no-backups'
        makeBackups = false
        break
      when '-v', '--verbose'
        VERBOSE = true
        break
      else
        files.push arg

  if showSyntax or files.length == 0
    return syntax()

  for filename in files
    verboseLog("[Reading] #{filename}")
    result = fixEssential(filename, makeBackups)
    console.log("[#{result}] #{filename}") # Always print this

  return 0

main()
