;-----------------------------------------------------------------------
;
; Retrodev SDK -- CPC FDC file routines.
;
; Purpose: floppy-disc file load/save helpers for FDC-based access.
;
; (c) TLOTB 2026
;
;-----------------------------------------------------------------------

IFNDEF __CPC_FILE_FDC_ASM__
__CPC_FILE_FDC_ASM__ equ 1

; Require macros.fdc.asm to be included before this file.
IFNDEF __MACROS_FDC_ASM__
FAIL 'cpc.file.fdc.asm requires macros.fdc.asm to be included first'
ENDIF

; CPC_FileLoadFDC -- load a named file from disc by driving the NEC uPD765 FDC directly.
;
; Reads a file from a standard AMSDOS-format CPC disc without using any firmware
; or ROM jump block. Suitable for loaders that run before AMSDOS is active, on
; machines where the disc ROM is not present, or in any context where the firmware
; CAS_ vectors are unavailable or unreliable.
;
; Disc format -- standard CPC Data disc:
;   40 tracks, 9 sectors/track, 512 bytes/sector, MFM double-density.
;   Sector IDs: #C1-#C9 on every track.
;   Block size: 1 KB = 2 adjacent sectors on the same track.
;   Blocks 0-1 = directory; file data starts at block 2.
;   Track 0, sectors #C1-#C4: directory (64 entries * 32 bytes = 2048 bytes).
;
; Directory entry layout (32 bytes):
;   Byte  0     : user number -- 0 = active; #E5 = deleted/unused
;   Bytes 1-8   : filename, space-padded (high bit = AMSDOS flag; mask with AND #7F)
;   Bytes 9-11  : extension, space-padded (high bit = AMSDOS flag; mask with AND #7F)
;   Byte  12    : extent number (0 = first extent)
;   Bytes 13-14 : reserved
;   Byte  15    : record count (1 record = 128 bytes)
;   Bytes 16-31 : block allocation map (16 * 1-byte block numbers; 0 = end of chain)
;
; AMSDOS file header (first 128 bytes of the first physical file sector):
;   Bytes 1-8   : filename (space-padded)
;   Bytes 9-11  : extension
;   Bytes 21-22 : load address (LE16)
;   Bytes 24-25 : file length low 16 bits (LE16)
;   Byte  66    : file length high byte
;   Byte  69    : checksum (sum of bytes 0-67, low byte)
;   File payload begins at byte offset HDR_SIZE (128) within the first sector.
;
; Block -> sector mapping:
;   Each 1K block N spans two adjacent sectors on the same track:
;     sector_index = N * 2
;     track  = sector_index / 9
;     sector = (sector_index mod 9) + #C1
;
; FDC port map (NEC uPD765):
;   #FB7E (r)   : main status register -- bit 7 = RQM (ready), bit 6 = DIO (direction)
;   #FA7E (r/w) : data register
;   #FB7F (w)   : motor control -- bit 0 = drive A, bit 1 = drive B
;
; FDC commands used (see macros.fdc.asm for full command documentation):
;   FDC_CMD_SPECIFY  : step rate, head timing, non-DMA; issued once at startup
;   FDC_CMD_SEEK     : move head to cylinder; confirmed via SENSE INTERRUPT STATUS
;   FDC_CMD_SENSE_INT: retrieve ST0 + PCN after seek
;   FDC_CMD_READ_SKIP: read data MFM skip-deleted; 8 command -> data -> 7 result bytes
;
; Motor spin-up: ~320 ms software delay after motor-on (minimum 300 ms required).
;
; Entry:
;   HL = pointer to 11-byte filename: 8 name bytes + 3 extension bytes,
;        space-padded to full length, upper-case ASCII
;   DE = destination address for file payload (AMSDOS 128-byte header stripped)
;   A  = drive: 0 = drive A, 1 = drive B
;
; Exit:
;   Carry set   = success; HL = total bytes written to destination
;   Carry clear = failure; A = 0 (not found) or non-zero (FDC error -- ST1 flags)
;
; Destroys: AF, BC, DE, HL, IY
; Preserves: IX, AF', BC', DE', HL'
;
; Uses the static FDC_SectorBuf (512 bytes) declared below.
; The caller's destination must not overlap FDC_SectorBuf.

; ============================================================
; Disc layout constants local to this file
; ============================================================
DISC_DIR_TRACK			equ 0
DISC_DIR_SECTORS		equ 4
DISC_BLOCKS_PER_ENTRY	equ 16

; ============================================================
; Directory entry offsets
; ============================================================
DIR_ENTRY_SIZE		equ 32
DIR_ENTRY_USER		equ 0
DIR_ENTRY_NAME		equ 1
DIR_ENTRY_EXT		equ 9
DIR_ENTRY_BLOCKS	equ 16

; ============================================================
; AMSDOS header size
; ============================================================
HDR_SIZE			equ 128

; ============================================================
; Static sector buffer (512 bytes -- must be in writable RAM)
; ============================================================
FDC_SectorBuf	ds FDC_DISC_SECTOR_SIZE

; ============================================================
; IY workspace layout (8 bytes allocated on the stack at entry)
;
;   [IY+0] = drive (0=A, 1=B)
;   [IY+1] = first-sector flag (1 = next copy must strip AMSDOS header; 0 = full copy)
;   [IY+2] = filename pointer low
;   [IY+3] = filename pointer high
;   [IY+4] = destination pointer low
;   [IY+5] = destination pointer high
;   [IY+6] = bytes loaded low
;   [IY+7] = bytes loaded high
; ============================================================

; CPC_FileLoadFDC -- load a named file from disc by driving the NEC uPD765 FDC directly.
; Entry: HL = pointer to 11-byte filename (8 name + 3 ext, space-padded, upper-case ASCII)
;        DE = destination address for file payload (AMSDOS 128-byte header stripped)
;        A  = drive: 0 = drive A, 1 = drive B
; Exit:  Carry set = success; HL = bytes written
;        Carry clear = failure; A = 0 (not found) or non-zero (FDC error)
; Destroys: AF, BC, DE, HL, IY  Preserves: IX
CPC_FileLoadFDC:
    ; Save drive then probe FDC: absent port returns #FF (floating bus).
    ld c,a
    ld b,FDC_PORT_STATUS_B
    in a,(c)
    cp #ff
    jp z,.ffd_no_disc
    ; Set up stack workspace; IY = workspace base.
    push iy
    push ix
    ld iy,-8
    add iy,sp
    ld sp,iy
    ld a,c				; drive (saved above)
    ld (iy+0),a			; drive
    ld (iy+1),1			; first-sector flag
    ld (iy+2),l			; filename ptr low
    ld (iy+3),h			; filename ptr high
    ld (iy+4),e			; dest low
    ld (iy+5),d			; dest high
    ld (iy+6),0			; bytes loaded low
    ld (iy+7),0			; bytes loaded high
    ; Enable motor for selected drive.
    ld a,(iy+0)
    or a
    jr nz,.ffd_motor_b
    FDC_MotorOn 0
    jr .ffd_motor_done
.ffd_motor_b:
    FDC_MotorOn 1
.ffd_motor_done:
    ; SPECIFY: SRT=6 ms, HUT=240 ms, HLT=4 ms, non-DMA.
    FDC_Specify
    ; Seek to track 0 before reading the directory.
    ld b,DISC_DIR_TRACK
    call .fdc_seek_to
    jp nc,.ffd_fail
    ; -------------------------------------------------------
    ; Directory scan: four sectors (#C1-#C4) on track 0.
    ; Outer loop (D/E) = per-sector; inner loop (B) = per-entry.
    ; -------------------------------------------------------
    ld d,DISC_DIR_SECTORS
    ld e,FDC_DISC_SECTOR_BASE
.ffd_dir_sector:
    ld a,(iy+0)
    ld b,DISC_DIR_TRACK
    ld c,e
    ld hl,FDC_SectorBuf
    call .fdc_read_sector
    jp nc,.ffd_fail
    ld hl,FDC_SectorBuf
    ld b,FDC_DISC_SECTOR_SIZE / DIR_ENTRY_SIZE
.ffd_dir_entry:
    ; Skip deleted (#E5) and non-zero user entries.
    ld a,(hl)
    cp #e5
    jr z,.ffd_entry_advance
    or a
    jr nz,.ffd_entry_advance
    ; Compare 11-byte filename (8 name + 3 ext) at entry+DIR_ENTRY_NAME.
    push de
    push bc
    push hl
    ld bc,DIR_ENTRY_NAME
    add hl,bc			; HL -> entry name[0]
    ld e,(iy+2)
    ld d,(iy+3)			; DE = caller's filename pointer
    ld c,11
.ffd_cmp:
    ld a,(hl)
    and #7f
    cp (de)
    jr nz,.ffd_cmp_fail
    inc hl
    inc de
    dec c
    jr nz,.ffd_cmp
    ; Match: restore entry base and process block list.
    pop hl
    pop bc
    pop de
    push de
    push bc
    call .ffd_process_entry
    pop bc
    pop de
    jp nc,.ffd_fail_fdc
    ; File loaded successfully -- stop scanning and exit.
    jp .ffd_success
.ffd_cmp_fail:
    pop hl
    pop bc
    pop de
.ffd_entry_advance:
    ld de,DIR_ENTRY_SIZE
    add hl,de
    dec b
    jr nz,.ffd_dir_entry
    inc e
    dec d
    jp nz,.ffd_dir_sector
    ; All directory sectors scanned -- file not found.
    jp .ffd_fail
.ffd_success:
    ; Success.
    ld l,(iy+6)
    ld h,(iy+7)
    ld bc,8
    add iy,bc
    ld sp,iy
    pop ix
    pop iy
    scf
    ret

.ffd_fail_fdc:
    ; A = ST1 error flags, carry already clear.
    push af
    ld bc,8
    add iy,bc
    ld sp,iy
    pop ix
    pop iy
    pop af
    or a
    ret
.ffd_fail:
    xor a
    ld bc,8
    add iy,bc
    ld sp,iy
    pop ix
    pop iy
    or a
    ret
.ffd_no_disc:
    xor a
    or a
    ret

; ============================================================
; .ffd_process_entry
; ============================================================
;
; Process the block allocation map of one matching directory entry.
; Reads each 1K block (2 sectors) and appends data to the destination.
;
; Entry: HL = entry base (within FDC_SectorBuf); IY = workspace.
; Exit:  Carry set = all blocks done; Carry clear = FDC error, A = ST1 flags.
; Destroys: AF, BC, DE, HL.

.ffd_process_entry:
    ld de,DIR_ENTRY_BLOCKS
    add hl,de			; HL -> block map[0]
    ld b,DISC_BLOCKS_PER_ENTRY
.fpe_loop:
    ; Read block number. 0 = end of allocation for this entry.
    ld a,(hl)
    inc hl
    or a
    jr z,.fpe_ok
    ; Save block map cursor (HL) and block counter (B).
    push hl
    push bc
    ; Convert block number A to track (B) and first sector ID (C).
    ; sector_index = A * 2 (both sectors of a 1K block share the same track).
    sla a				; A = sector_index (fits in 8 bits: max block < 80 on 40-track disc)
    ld h,0				; H = track counter
.fpe_div9:
    sub 9
    jr c,.fpe_div9_done
    inc h				; track++
    jr .fpe_div9
.fpe_div9_done:
    ; A underflowed by one subtraction: add 9 back to recover the remainder.
    add a,9				; A = sector offset within track (0-8)
    add a,FDC_DISC_SECTOR_BASE	; A = first sector ID (#C1-#C9)
    ld c,a				; C = first sector ID
    ld b,h				; B = track
    ; Seek to this track.
    call .fdc_seek_to
    jr nc,.fpe_fail
    ; Read first sector into FDC_SectorBuf.
    ld a,(iy+0)			; drive
    ld hl,FDC_SectorBuf
    call .fdc_read_sector		; B=track, C=first-sector-ID
    jr nc,.fpe_fail
    ; Push track (B) and first sector ID (C) before copying (copy trashes BC).
    push bc
    call .fpe_copy_sector
    pop bc
    ; Read second sector (same track, sector ID = first + 1).
    inc c
    ld a,(iy+0)
    ld hl,FDC_SectorBuf
    call .fdc_read_sector
    jr nc,.fpe_fail
    call .fpe_copy_sector
    ; Restore block map cursor and counter.
    pop bc
    pop hl
    dec b
    jr nz,.fpe_loop
.fpe_ok:
    scf
    ret
.fpe_fail:
    pop bc
    pop hl
    or a
    ret

; ============================================================
; .fpe_copy_sector
; ============================================================
;
; Copy FDC_SectorBuf to the destination stored in workspace [IY+4/5].
; Strips the AMSDOS header (HDR_SIZE bytes) on the first call ([IY+1] != 0).
; Updates [IY+1] (first-sector flag), [IY+4/5] (dest pointer), [IY+6/7] (bytes loaded).
;
; Entry: IY = workspace; FDC_SectorBuf contains the freshly-read sector.
; Exit:  workspace updated; BC, DE, HL destroyed.

.fpe_copy_sector:
    ; Determine source and byte count based on first-sector flag.
    ld a,(iy+1)
    or a
    jr z,.fcs_full_sector
    ; First file sector: skip AMSDOS header.
    ld hl,FDC_SectorBuf + HDR_SIZE
    ld bc,FDC_DISC_SECTOR_SIZE - HDR_SIZE
    ld (iy+1),0			; clear first-sector flag
    jr .fcs_copy
.fcs_full_sector:
    ld hl,FDC_SectorBuf
    ld bc,FDC_DISC_SECTOR_SIZE
.fcs_copy:
    ; Save byte count for the bytes_loaded update (ldir zeroes BC).
    push bc
    ; Load destination from workspace.
    ld e,(iy+4)
    ld d,(iy+5)
    ; ldir: HL=source, DE=dest, BC=count.
    ldir
    ; Store updated destination pointer.
    ld (iy+4),e
    ld (iy+5),d
    ; Restore byte count and add to bytes_loaded.
    pop bc				; BC = bytes just copied
    ld l,(iy+6)
    ld h,(iy+7)
    add hl,bc
    ld (iy+6),l
    ld (iy+7),h
    ret

; ============================================================
; FDC low-level subroutines
; ============================================================

; .fdc_seek_to -- seek head to cylinder B on drive [IY+0].
; Issues SEEK then SENSE INTERRUPT STATUS; checks ST0 for normal completion.
; Entry: B = target cylinder.
; Exit:  Carry set = success; Carry clear = FDC error.
; Destroys: AF, BC.
.fdc_seek_to:
    FDC_WriteByte FDC_CMD_SEEK
    ld a,(iy+0)
    FDC_WriteByte a
    FDC_WriteByte b
    ; Poll FDC_MSR_RQM until seek completes (RQM rises after interrupt).
    ld bc,FDC_PORT_STATUS
.fdc_seek_poll:
    in a,(c)
    and FDC_MSR_RQM
    jr z,.fdc_seek_poll
    ; SENSE INTERRUPT STATUS -> ST0, PCN.
    FDC_WriteByte FDC_CMD_SENSE_INT
    FDC_ReadByte				; ST0
    ld c,a
    FDC_ReadByte				; PCN (discard)
    ; Bits 7-6 of ST0: 00 = normal termination; anything else = error.
    ld a,c
    and FDC_ST0_IC_MASK
    jr nz,.fdc_seek_err
    scf
    ret
.fdc_seek_err:
    or a
    ret

; .fdc_read_sector -- issue READ DATA and poll one 512-byte sector into a buffer.
;
; Entry:
;   A  = drive (0=A, 1=B)
;   B  = track (cylinder)
;   C  = sector ID (#C1-#C9)
;   HL = 512-byte destination buffer
; Exit:
;   Carry set   = success.
;   Carry clear = FDC error; A = ST1 flags.
; Destroys: AF, BC, DE, HL.
.fdc_read_sector:
    push hl				; save buffer
    ld d,a				; D = drive
    FDC_WriteByte FDC_CMD_READ_SKIP
    FDC_WriteByte d
    push bc
    FDC_WriteByte b			; cylinder
    FDC_WriteByte 0			; head (always 0)
    pop bc
    push bc
    FDC_WriteByte c			; sector ID
    FDC_WriteByte FDC_DISC_SECTOR_N	; N=2 (512 bytes)
    FDC_WriteByte FDC_DISC_SECTOR_BASE + FDC_DISC_SECTORS - 1	; EOT
    FDC_WriteByte FDC_DISC_GPL_RW	; GPL
    FDC_WriteByte FDC_DISC_DTL		; DTL
    pop bc				; discard saved track/sector
    ; Data transfer phase: poll RQM+DIO then read each byte into buffer.
    pop hl				; restore buffer pointer
    ld de,FDC_DISC_SECTOR_SIZE		; DE = bytes remaining
.fdc_data_poll:
    ld bc,FDC_PORT_STATUS
    in a,(c)
    and FDC_MSR_RQM | FDC_MSR_DIO
    cp FDC_MSR_RQM | FDC_MSR_DIO
    jr nz,.fdc_data_poll
    ld bc,FDC_PORT_DATA
    in a,(c)
    ld (hl),a
    inc hl
    dec de
    ld a,d
    or e
    jr nz,.fdc_data_poll
    ; Result phase: ST0, ST1, ST2, C, H, R, N.
    FDC_ReadByte			; ST0
    ld d,a
    FDC_ReadByte			; ST1
    ld e,a
    FDC_ReadByte			; ST2 (discard)
    FDC_ReadByte			; C   (discard)
    FDC_ReadByte			; H   (discard)
    FDC_ReadByte			; R   (discard)
    FDC_ReadByte			; N   (discard)
    ; Check ST0 IC bits for abnormal termination.
    ld a,d
    and FDC_ST0_IC_MASK
    jr nz,.fdc_rs_err
    ; Any ST1 error bit = failure.
    ld a,e
    or a
    jr nz,.fdc_rs_err
    scf
    ret
.fdc_rs_err:
    ld a,e
    or a
    ret


ENDIF
