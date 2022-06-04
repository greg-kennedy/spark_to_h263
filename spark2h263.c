// spark2h263.c : This file contains the 'main' function. Program execution begins and ends there.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitstream.h"
#include "hufftbl.h"

// defines
#define I_FRAME 0
#define P_FRAME 1

#define vlc_read(x, y) { \
    printf(#x ": "); \
    x = get_bits(&in, y); \
    printf(" = %d\n", x); \
    if (x < 0) return -1; \
}

static int decode_block(struct bitstream* in, struct bitstream* out, unsigned char intradc, unsigned char block_exists, unsigned char flv_version)
{
    if (intradc) {
        printf("  INTRADC: ");
        copy_bits(in, out, 8);
        printf("\n");
    }

    if (block_exists) {
        printf("  TCOEF:\n");
        int last = 0;
        while (!last) {
            printf("   TCOEF HUFF: ");
            int flags = huff_lookup(in, out, TCOEF_HUFF);
            if (flags < 0) { fprintf(stderr, "TCOEF huff_lookup failed\n"); return -1; }

            if (flags == 2) {
                printf(" (extended range)\n");
                //                printf("*");
                                // ESCAPE
                                //  there are 3 outcomes here depending on flv_version and the first bit
                                // non-extended-quant-table mode
                int level_width;
                if (flv_version == 0)
                    level_width = 8;
                else {
                    printf("  width bit: ");
                    if (get_bit(in))
                        level_width = 11;   // extended range level
                    else
                        level_width = 7;    // limited range level
                }

                // last
                printf("  last_bit: ");
                last = copy_bit(in, out);
                // run
                printf(", run: ");
                copy_bits(in, out, 6);
                // level
                printf(", level: ");
                unsigned int level = get_bits(in, level_width);

                // silly sign extension
                if (level_width == 7) {
                    level = level | ((level & 0b01000000) ? 0b1111111110000000 : 0);
                }
                else if (level_width == 8) {
                    level = level | ((level & 0b10000000) ? 0b1111111100000000 : 0);
                }
                // the h.263 standard is to write 8 bits Level if it's within -127 to 127
                //  otherwise, write -128, and then 11 bits level
                if (level <= 0b00001111111 || level >= 0b11110000001) {
                    put_bits(out, 8, level);
                }
                else {
                    put_bits(out, 8, 0b10000000);
                    put_bits(out, 5, level & 0b11111);
                    put_bits(out, 6, (level >> 5) & 0b111111);
                }

//                if (level == 0)
//                    fprintf(stderr, "forbidden level\n");
            }
            else {
                last = flags;
                // copy sign bit
                copy_bit(in, out);
            }
            printf("\n");
        }
    }
    //    printf("\n");
    return 0;
}

static int copy_umv(struct bitstream* in, struct bitstream* out)
{
    // default to stuffing needed
    int stuffing = 1;

    for (int i = 0; i < 2; i++) {
        printf("  UMV %s: ", (i ? "horiz" : "vert" ));
        if (!copy_bit(in, out)) {
            // leading 0
            //  test for Stuffing bit
            int val;
            do {
                // val
                val = copy_bits(in, out, 2);
                if (val) stuffing = 0;
            } while (val & 1);
        }
        else {
            stuffing = 0;
        }
        printf("\n");
    }

    // six 0 in a row means add 1 stuffing bit
    if (stuffing) {
        printf("  (stuffing): ");  copy_bit(in, out); printf("\n");
    }

    return 0;
}

static int copy_mv(struct bitstream* in, struct bitstream* out)
{
    for (int i = 0; i < 2; i++) {
        printf("  MV %s: ", (i ? "horiz" : "vert"));
        huff_lookup(in, out, MVD_HUFF);
        printf("\n");
    }

    return 0;
}

// skip bits from in, until a picture header is found
static int locate_picture(struct bitstream * in)
{
    printf("Searching picture header: ");
    int buffer = get_bits(in, 17);
    printf(" = %d\n", buffer);
    while (buffer != 1)
    {
        // check topmost bit
        if (buffer & 0x10000) { fprintf(stderr, " . WARNING: skipping non-padding bits\n"); }
        // blank it
        buffer &= 0xFFFF;
        // read next one
        printf("Advance: ");
        int bit = get_bit(in);
        printf(" = %d\n", bit);
        if (bit < 0) {
            // out of bits on read
            return -1;
        }
        else {
            buffer = (buffer << 1) | bit;
        }
    }

    return 0;
}

int main(int argc, char * argv[])
{
    struct bitstream in, out;

    char* path_in = NULL, * path_out = NULL;
    unsigned char par_w = 0, par_h = 0, par = 0;
    unsigned char pcf_div = 0, pcf_conv = 0, custom_pcf = 0;

    int etr = 0, tr = 0;

    // parse CLI options
    if (argc < 3) {
        printf("Usage: %s [-r framerate] [-p par] in.spark out.h263p\n", argv[0]);
        return EXIT_SUCCESS;
    }
    for (int i = 1; i < argc; i++)
    {
        if (stricmp(argv[i], "-r") == 0)
        {
            if (sscanf(argv[i + 1], "%hhu,%hhu", &pcf_div, &pcf_conv) != 2) {
                fprintf(stderr, "Failed to parse framerate %s (expected format: 60,1)\n", argv[i + 1]);
                return EXIT_FAILURE;
            }
        }
        else if (stricmp(argv[i], "-p") == 0) {
            if (sscanf(argv[i + 1], "%hhu:%hhu", &par_w, &par_h) != 2) {
                fprintf(stderr, "Failed to parse PAR %s (expected format: 1:1)\n", argv[i + 1]);
                return EXIT_FAILURE;
            }
        }
        else if (path_in == NULL) {
            path_in = argv[i];
        }
        else if (path_out == NULL) {
            path_out = argv[i];
        }
        else {
            fprintf(stderr, "Unrecognized command-line argument '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (path_in == NULL) {
        fprintf(stderr, "Missing input file name\n");
        return EXIT_FAILURE;
    }
    else if (path_out == NULL) {
        fprintf(stderr, "Missing output file name\n");
        return EXIT_FAILURE;
    }

    if (pcf_div == 0 || pcf_conv == 0) {
        printf("Note: Frame rate not specified, defaulting to 30fps (60, 0)\n");
        pcf_div = 60;
        pcf_conv = 0;
    }
    if (pcf_conv != 60 || pcf_div != 1) {
        custom_pcf = 1;
    }
    if (par_w == 0 || par_h == 0) {
        printf("Note: PAR not specified, defaulting to 1:1\n");
        par_w = par_h = 1;
    }
    // PAR (there's a table...)
    if (par_w == par_h == 1) {
        par = 1;
    }
    else if (par_w == 12 && par_h == 11) {
        par = 2;
    }
    else if (par_w == 10 && par_h == 11) {
        par = 3;
    }
    else if (par_w == 16 && par_h == 11) {
        par = 4;
    }
    else if (par_w == 40 && par_h == 33) {
        par = 5;
    }
    else {
        par = 15;
    }

    // Open input and output files
    in.fp = fopen(path_in, "rb");
    if (!in.fp) {
        perror(path_in);
        return EXIT_FAILURE;
    }
    in.mask = 0;
    in.byte = 0;

    out.fp = fopen(path_out, "wb");
    if (!out.fp) {
        perror(path_out);
        return EXIT_FAILURE;
    }
    out.mask = 0x80;
    out.byte = 0;

    // read first bits
    while (locate_picture(&in) == 0)
    {
        /* picture header */
        int flv_version;
        vlc_read(flv_version, 5);
        if (flv_version != 0 && flv_version != 1) {
            fprintf(stderr, "Bad FLV verion %d\n", flv_version);
            return -1;
        }

        int picture_number; vlc_read(picture_number, 8); /* picture timestamp */
        if (tr > picture_number) {
            // rollover detection for ETR
            etr = (etr + 1) & 0x03;
        }
        tr = picture_number;

        int format;
        vlc_read(format, 3);

        int width, height;
        switch (format) {
        case 0:
            vlc_read(width, 8);
            vlc_read(height, 8);
            break;
        case 1:
            vlc_read(width, 16);
            vlc_read(height, 16);
            break;
        case 2:
            width = 352;
            height = 288;
            break;
        case 3:
            width = 176;
            height = 144;
            break;
        case 4:
            width = 128;
            height = 96;
            break;
        case 5:
            width = 320;
            height = 240;
            break;
        case 6:
            width = 160;
            height = 120;
            break;
        default:
            width = height = 0;
            break;
        }
        if (width <= 0 || height <= 0)
        {
            fprintf(stderr, "Invaid format (%d) => width / height %d x %d\n", format, width, height);
        }

        int p_type, droppable;
        int frame_type;
        vlc_read(frame_type, 2);

        switch (frame_type)
        {
        case 0:
            // I FRAME
            p_type = 0;
            droppable = 0;
            break;
        case 1:
            // P FRAME
            p_type = 1;
            droppable = 0;
            break;
        case 2:
            // Droppable P FRAME
            p_type = 1;
            droppable = 1;
            break;
        default:
            // read error or reserved value
            p_type = droppable = -1;
        }
        if (p_type < 0 || droppable < 0)
        {
            fprintf(stderr, "Invaid p_type (%d) / droppable (%d)\n", p_type, droppable);
            return -1;
        }

        int deblocking;
        vlc_read(deblocking, 1);
        if (deblocking < 0) {
            fprintf(stderr, "Invalid deblocking bit\n");
            return -1;
        }

        int qscale;
        vlc_read(qscale, 5);
        if (qscale < 0) {
            fprintf(stderr, "Bad qscale value %d\n", qscale);
            return -1;
        }

        printf("Frame %04d: flv_ver = %d, fmt = %d (%d x %d), %c%c, deblock = %d, qscale = %d\n  file pos = %d, mask = %02x\n",
            (etr << 8) | tr, flv_version, format, width, height, (p_type ? 'P' : 'I'),
            droppable ? 'D' : ' ', deblocking, qscale, ftell(in.fp), in.mask);

        // write new header
        //  PSC must be byte-aligned so insert stuffing if mask is nonzero
        while (out.mask != 0x80) {
            put_bit(&out, 0);
        }
        // PSC
        put_bits(&out, 22, 0b0000'0000'0000'0000'1'00000);
        // TR
        put_bits(&out, 8, picture_number);
        // PTYPE
        // There are predefined H.263 formats available here, if the video
        //  meets specific restrictions (29.97fps, 12:11 PAR and xCIF resolution)
        // however, PLUSPTYPE is required for Annex T support, so always use
        //  an H.263+ header instead.
        // also, extended motion vectors are implicitly part of H.263+
        put_bits(&out, 8, 0b10000'111 );

        // PLUSPTYPE
        // UFEP - we will write one if I and not if P
        if (p_type)
        {
            put_bits(&out, 3, 0);
        } else {
            put_bits(&out, 3, 1);

            // OPPTYPE
            // Source format (always Custom)
            put_bits(&out, 3, 0b110);
            // custom PCF
            put_bits(&out, 1, custom_pcf);
            // UMV always 1 on Spark
//            put_bits(&out, 1, 1);
            put_bits(&out, 1, 0);
            // SAC off
            put_bits(&out, 1, 0);
            // AP always ON
            put_bits(&out, 1, 1);
            // advanced INTRA always off
            put_bits(&out, 1, 0);
            // Deblocking mode (annex J)
            put_bits(&out, 1, 0);
//            put_bits(&out, 1, deblocking);
            // SS, RPS, IDS, AIV
            put_bits(&out, 4, 0);
            // MQ Table (Annex T)
            put_bits(&out, 1, 1);
            // addl. reserved 4 bits
            put_bits(&out, 4, 0b1000);
        }

        // MPPTYPE
        put_bits(&out, 3, p_type);
        put_bits(&out, 6, 1);

        // CPM
        put_bit(&out, 0);
        // no PSBI

        // CPFMT if I-Frame
        if (! p_type)
        {
            // PAR
            put_bits(&out, 4, par);
            // Width
            put_bits(&out, 9, (width / 4) - 1);
            // always 1
            put_bit(&out, 1);
            // Height
            put_bits(&out, 9, (height / 4));

            // EPAR
            if (par == 15) {
                put_bits(&out, 8, par_w);
                put_bits(&out, 8, par_h);
            }

            // CPCFC
            if (custom_pcf) {
                put_bit(&out, pcf_conv);
                put_bits(&out, 7, pcf_div);
            }
        }

        // ETR (if CPCFC)
        if (custom_pcf) {
            put_bits(&out, 2, etr);
        }

        // UUI (if UFEP)
//        if (! p_type) {
//            put_bits(&out, 2, 1);
//        }

        // omitting a large number of deselected mode fields here

        // PQUANT
        put_bits(&out, 5, qscale);

        // ftell
//        printf("Current position: in = %d, out = %d\n", ftell(in.fp), ftell(out.fp));
        /////////
        /* PEI */
        printf("PEI: ");
        while (copy_bit(&in, &out)) {
            copy_bits(&in, &out, 8);
        }
        printf("\n");

        printf(" . MB layer\n");

        // macroblock layer
        int mb_count = ((height + 15) / 16) * ((width + 15) / 16);
        for (int mb = 0; mb < mb_count; mb ++)
        {
            printf("MB %d of %d: ", mb, mb_count);

            int MCBPC;
            if (!p_type) {
                // I FRAME always CODed
                printf(" MCBPC I: ");
                MCBPC = huff_lookup(&in, &out, MCBPC_I_HUFF);
            } else {
                // P FRAME maybe CODed
                printf(" COD: ");
                if (copy_bit(&in, &out) == 1) {
                    printf("\n");
                    continue;
                }
                printf("\n");
                printf(" MCBPC P: ");
                MCBPC = huff_lookup(&in, &out, MCBPC_P_HUFF);
            }
            printf("\n");

            if (MCBPC < 0) { fprintf(stderr, "MCBPC huff_lookup failed\n"); return EXIT_FAILURE; }

            // other block info follows
            // CBPY
            printf(" CBPY: ");
            int CBPY = huff_lookup(&in, &out, CBPY_HUFF);
            printf("\n");
            if (CBPY < 0) { fprintf(stderr, "CBPY huff_lookup failed\n"); return EXIT_FAILURE; }

            if (! (MCBPC & INTRADC)) CBPY = CBPY ^ 0b1111;

            // DQUANT
            //  note that this uses the Annex T spec for variable len DQUANT
            if (MCBPC & DQUANT) {
                printf(" DQUANT: ");
                unsigned int dq_bit = copy_bit(&in, &out);
                if (dq_bit) {
                    put_bits(&out, 5, get_bits(&in, 5));
                }
                else {
                    put_bit(&out, get_bit(&in));
                }
                printf("\n");
            }

            if (MCBPC & MVD) {
                // motion vectors - this is UMV mode
                printf(" MVD0:\n");
                copy_mv(&in, &out);
//                copy_umv(&in, &out);
            }

            if (MCBPC & MVD24) {
                // MVD2-4
                for (int i = 1; i < 4; i++)
                {
                    printf(" MVD%d:\n", i);
                    copy_mv(&in, &out);
//                    copy_umv(&in, &out);
                }
            }

            // BLOCK LAYER
            // 4x luma
            for (int i = 0; i < 4; i++) {
                if ((MCBPC & INTRADC) || (CBPY & (0b1000 >> i))) {
                    printf(" LUMA%d\n", i);
                    if (decode_block(&in, &out, MCBPC & INTRADC, CBPY & (0b1000 >> i), flv_version) < 0)
                        return EXIT_FAILURE;
                }
            }
            // 2x chroma
            for (int i = 0; i < 2; i++) {
                if ((MCBPC & INTRADC) || (MCBPC & (0b10 >> i))) {
                    printf(" CHROMA%d\n", i);
                    if (decode_block(&in, &out, MCBPC & INTRADC, MCBPC & (0b10 >> i), flv_version) < 0)
                        return EXIT_FAILURE;
                }
            }
        }
        printf("\n");
    }

    // have to write the remaining byte from out and close files
    fclose(in.fp);
    if (out.mask != 0x80) {
        if (fwrite(&out.byte, 1, 1, out.fp) != 1)
        {
            perror("fwrite");
            return -1;
        }
    }
    fclose(out.fp);

    return 0;
}