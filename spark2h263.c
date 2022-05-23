// spark2h263.c : This file contains the 'main' function. Program execution begins and ends there.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// defines
#define I_FRAME 0
#define P_FRAME 1

#define INTRADC 0b1000'00
#define DQUANT 0b100'00
#define MVD 0b10'00
#define MVD24 0b1'00
#define PADDING 0x80
#define EOT 0xFF

// tables
//  huff table format is (len, val, result)
// some of the val have >8 bits but since the topmost are all 0
//  they still fit in an unsigned char...
unsigned char MCBPC_I_HUFF[][3] = {
    {1, 0b1,INTRADC | 0},
    {3, 0b001, INTRADC | 1},
    {3, 0b010, INTRADC | 2},
    {3, 0b011, INTRADC | 3},

    {4, 0b0001, INTRADC | DQUANT | 0},
    {6, 0b0000'01, INTRADC | DQUANT | 1},
    {6, 0b0000'10, INTRADC | DQUANT | 2},
    {6, 0b0000'11, INTRADC | DQUANT | 3},

    {9, 0b0000'0000'1, PADDING}, // padding

    {EOT, EOT, EOT} // terminator
};

unsigned char MCBPC_P_HUFF[][3] = {
    {1, 0b1, MVD | 0},
    {4, 0b0011, MVD | 1},
    {4, 0b0010, MVD | 2},
    {6, 0b0001'01, MVD | 3},

    {3, 0b011, DQUANT | MVD | 0},
    {7, 0b0000'111, DQUANT | MVD | 1},
    {7, 0b0000'110, DQUANT | MVD | 2},
    {9, 0b0000'0010'1, DQUANT | MVD | 3},

    {3, 0b010, MVD | MVD24 | 0},
    {7, 0b0000'101, MVD | MVD24 | 1},
    {7, 0b0000'100, MVD | MVD24 | 2},
    {8, 0b0000'101, MVD | MVD24 | 3},

    {5, 0b0001'1, INTRADC | 0},
    {8, 0b0000'0100, INTRADC | 1},
    {8, 0b0000'0011, INTRADC | 2},
    {7, 0b0000'001, INTRADC | 3},

    {6, 0b0001'00, INTRADC | DQUANT | 0},
    {9, 0b0000'0010'0, INTRADC | DQUANT | 1},
    {9, 0b0000'0001'1, INTRADC | DQUANT | 2},
    {9, 0b0000'0001'0, INTRADC | DQUANT | 3},

    {9, 0b0000'0000'1, PADDING}, // padding

    /* these only exist for AP or Deblock mode */
    {11, 0b0000'0000'010, DQUANT | MVD | MVD24 | 0},
    {13, 0b0000'0000'0110'0, DQUANT | MVD | MVD24 | 1},
    {13, 0b0000'0000'0111'0, DQUANT | MVD | MVD24 | 2},
    {13, 0b0000'0000'0111'1, DQUANT | MVD | MVD24 | 3},

    {EOT, EOT, EOT} // terminator
};

unsigned char CBPY_HUFF[][3] = {
    {4, 0b0011, 0},
    {5, 0b0010'1, 1},
    {5, 0b0010'0, 2},
    {4, 0b1001, 3},
    {5, 0b0001'1, 4},
    {4, 0b0111, 5},
    {6, 0b000010, 6},
    {4, 0b1011, 7},
    {5, 0b0001'0, 8},
    {6, 0b0000'11, 9},
    {4, 0b0101, 10},
    {4, 0b1010, 11},
    {4, 0b0100, 12},
    {4, 0b1000, 13},
    {4, 0b0110, 14},
    {2, 0b11, 15},
    {EOT, EOT, EOT} // term
};

// hufftbl lookup
static int huff_lookup(struct bitstream * bs, unsigned int* tbl[3])
{
    int done = 0;
    int len = 0;
    int val = 0;
    while (! done) {
        // read another bit
        val = (val << 1) | get_bit(bs);
        len++;

        // check table for match on len and val
        int i = 0;
        done = 1;
        while (tbl[i][0] != EOT) {
            if (len == tbl[i][0] && val == tbl[i][1]) return i;
            if (len < tbl[i][0]) done = 0;
            i++;
        }
    }
    fprintf(stderr, "hufftbl lookup failed! val=%d, len=%d\n", val, len);
    return -1;
}

// bitstream structs
struct bitstream
{
    FILE* fp;
    unsigned char byte;
    unsigned char mask;
};

static int get_bit(struct bitstream * bs)
{
    // gets the next bit of FP
    if (! bs->mask)
    {
        // out of bits in byte, need to read next
        if (fread(&bs->byte, 1, 1, bs->fp) != 1)
        {
            if (feof(bs->fp))
                fprintf(stderr, "at eof\n");
            else
                perror("fread");
            return -1;
        }
        bs->mask = 0x80;
    }

    int retval = (bs->byte & bs->mask) ? 1 : 0;
    bs->mask >>= 1;
    return retval;
}

static int get_bits(struct bitstream * bs, int count)
{
    int retval = 0;
    while (count) {
        int bit = get_bit(bs);
        if (bit < 0) return bit;
        retval = (retval << 1) | bit;
        count--;
    }
    return retval;
}

// functions to write to FP
static int put_bit(struct bitstream* bs, const unsigned int bit)
{
    if (bit)
        bs->byte |= bs->mask;
    bs->mask >>= 1;

    if (!bs->mask)
    {
        // filled up that byte, need to dump it
        if (fwrite(&bs->byte, 1, 1, bs->fp) != 1)
        {
            perror("fwrite");
            return -1;
        }
        bs->byte = 0;
        bs->mask = 0x80;
    }

    return 0;
}

static int put_bits(struct bitstream* bs, const int count, const unsigned int val)
{
    unsigned int mask = 0x01 << (count - 1);
    while (mask) {
        if (put_bit(bs, val & mask) < 0) return -1;
        mask >>= 1;
    }
    return 0;
}

// copy one bit from in to out, and also return the bit
static int copy_bit(struct bitstream* in, struct bitstream* out)
{
    int bit = get_bit(in);
    if (bit >= 0) put_bit(out, bit);
    return bit;
}

// copy bits from in to out, also ret the result
static int copy_bits(struct bitstream* in, struct bitstream* out, const int count)
{
    int bits = get_bits(in, count);
    if (bits >= 0) put_bits(out, count, bits);
    return bits;
}

// copy bits from in to out, until a picture header is found
static int locate_picture(struct bitstream * in, struct bitstream * out)
{
    int buffer = get_bits(in, 17);
    while (buffer != 1)
    {
        // write topmost bit
        put_bit(out, buffer & 0x10000);
        // blank it
        buffer &= 0xFFFF;
        // read next one
        int bit = get_bit(in);
        if (bit < 0) {
            // out of bits on read
            //  dump remaining to out
            put_bits(out, buffer, 16);
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
    while (locate_picture(&in, &out) == 0)
    {
        /* picture header */
        int flv_version = get_bits(&in, 5);
        if (flv_version != 0 && flv_version != 1) {
            fprintf(stderr, "Bad FLV verion %d\n", flv_version);
            return -1;
        }

        int picture_number = get_bits(&in, 8); /* picture timestamp */
        if (tr > picture_number) {
            // rollover detection for ETR
            etr = (etr + 1) & 0x03;
        }
        tr = picture_number;
        int format = get_bits(&in, 3);
        int width, height;
        switch (format) {
        case 0:
            width = get_bits(&in, 8);
            height = get_bits(&in, 8);
            break;
        case 1:
            width = get_bits(&in, 16);
            height = get_bits(&in, 16);
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
        switch (get_bits(&in, 2))
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

        int deblocking = get_bit(&in);
        if (deblocking < 0) {
            fprintf(stderr, "Invalid deblocking bit\n");
            return -1;
        }

        int qscale = get_bits(&in, 5);
        if (qscale < 0) {
            fprintf(stderr, "Bad qscale value %d\n", qscale);
            return -1;
        }

        printf("Frame %04d: flv_ver = %d, fmt = %d (%d x %d), %c%c, deblock = %d, qscale = %d\n",
            (etr << 8) | tr, flv_version, format, width, height, (p_type ? 'P' : 'I'),
            droppable ? 'D' : ' ', deblocking, qscale);

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
            put_bits(&out, 1, 1);
            // SAC, AP, advanced INTRA
            put_bits(&out, 3, 0);
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
        if (! p_type) {
            put_bits(&out, 2, 1);
        }

        // omitting a large number of deselected mode fields here

        // PQUANT
        put_bits(&out, 5, qscale);

        // ftell
//        printf("Current position: in = %d, out = %d\n", ftell(in.fp), ftell(out.fp));
        /////////
        /* PEI */
        while (copy_bit(&in, &out)) {
            copy_bits(&in, &out, 8);
        }

        // macroblock layer
        for (int y = 0; y < height; y += 16)
        {
            for (int x = 0; x < width; x += 16)
            {
                unsigned char MCBPC;
                if (!p_type) {
                    // I FRAME always CODed
                    int idx = huff_lookup(&in, MCBPC_I_HUFF);
                    put_bits(&out, MCBPC_I_HUFF[idx][0], MCBPC_I_HUFF[idx][1]);
                    MCBPC = MCBPC_I_HUFF[idx][2];
                } else {
                    // P FRAME maybe CODed
                    if (copy_bit(&in, &out) == 0) {
                        continue;
                    }
                    int idx = huff_lookup(&in, MCBPC_P_HUFF);
                    put_bits(&out, MCBPC_P_HUFF[idx][0], MCBPC_P_HUFF[idx][1]);
                    MCBPC = MCBPC_P_HUFF[idx][2];
                }

                // other block info follows
                // CBPY
                int idx = huff_lookup(&in, CBPY_HUFF);
                put_bits(&out, CBPY_HUFF[idx][0], CBPY_HUFF[idx][1]);
                int CBPY = CBPY_HUFF[idx][2];
                if (p_type) CBPY = (CBPY ^ 0b11) & 0b11;

                // DQUANT
                //  note that this uses the Annex T spec for variable len DQUANT
                if (MCBPC & DQUANT) {
                    unsigned int dq_bit = get_bit(&in);
                    put_bit(&out, dq_bit);
                    if (dq_bit) {
                        put_bits(&out, 5, get_bits(&in, 5));
                    }
                    else {
                        put_bit(&out, get_bit(&in));
                    }
                }

                if (MCBPC & MVD) {
                    // motion vectors - this is UMV mode
                    // horiz
                    if (!copy_bit(&in, &out)) {
                        do {
                            copy_bit(&in, &out);
                        } while (copy_bit(&in, &out));
                    }
                    // vert
                    if (!copy_bit(&in, &out)) {
                        do {
                            copy_bit(&in, &out);
                        } while (copy_bit(&in, &out));
                    }
                }
                if (MCBPC & MVD24) {
                    // MVD2-4
                    // screw it
                    fprintf(stderr, "Giving up at MVD2-4 haha\n");
                    return EXIT_FAILURE;
                }

                // BLOCK LAYER
                // 4x luma
                for (int i = 0; i < 4; i++) {
                    if (MCBPC & INTRADC)
                        copy_bits(&in, &out, 8);
                    if (CBPY & (1 << i)) {
                        // TCOEF
                    }
                }
                // 2x chroma
                for (int i = 0; i < 2; i++) {
                    if (MCBPC & INTRADC)
                        copy_bits(&in, &out, 8);
                    if (MCBPC & (1 << i)) {
                        // TCOEF
                    }
                }

            }
            printf("\n");
        }
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

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
