// spark2h263.c : This file contains the 'main' function. Program execution begins and ends there.
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// defines
#define I_FRAME 0
#define P_FRAME 1

#define FMT_CUSTOM 0
#define FMT_SQCIF 1
#define FMT_QCIF 2
#define FMT_CIF 3
#define FMT_4CIF 4
#define FMT_16CIF 5

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
static int put_bit(struct bitstream* bs, const int bit)
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

static int put_bits(struct bitstream* bs, const int count, const int val)
{
    unsigned int mask = 0x01 << (count - 1);
    while (mask) {
        if (put_bit(bs, val & mask) < 0) return -1;
        mask >>= 1;
    }
    return 0;
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

    if (argc != 3) {
        printf("Usage: %s in.spark out.h263\n", argv[0]);
        return EXIT_SUCCESS;
    }

    in.fp = fopen(argv[1], "rb");
    if (!in.fp) {
        perror("fopen");
        return EXIT_FAILURE;
    }
    in.mask = 0;
    in.byte = 0;

    out.fp = fopen(argv[2], "wb");
    if (!out.fp) {
        perror("fopen");
        return EXIT_FAILURE;
    }
    out.mask = 0x80;
    out.byte = 0;

    // read first bits
    while (locate_picture(&in, &out) == 0)
    {
        /* picture header */
        int flv_format = get_bits(&in, 5);
        if (flv_format != 0 && flv_format != 1) {
            fprintf(stderr, "Bad picture format %d\n", flv_format);
            return -1;
        }

        int picture_number = get_bits(&in, 8); /* picture timestamp */
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
        case 3:
            // Droppable P FRAME
            p_type = 1;
            droppable = 1;
            break;
        default:
            // read error
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

        printf("Frame %03d: flv_fmt = %d, fmt = %d (%d x %d), %c %c, deblock = %d, qscale = %d\n",
            picture_number, flv_format, format, width, height, (p_type ? 'P' : 'I'),
            droppable ? 'D' : ' ', deblocking, qscale);

        // write new header
        //  PSC must be byte-aligned so insert stuffing if mask is nonzero
        while (out.mask != 0x80) {
            put_bit(&out, 0);
        }
        // PSC
        put_bits(&out, 22, 0x20);
        // TR
        put_bits(&out, 8, picture_number);
        // PTYPE
        put_bits(&out, 5, 0x10);

        int plusptype = 0;
        if (width == 128 && height == 96) {
            // SQCIF
            put_bits(&out, 3, 1);
        } else if (width == 176 && height == 144) {
            // QCIF
            put_bits(&out, 3, 2);
        }
        else if (width == 352 && height == 288) {
            // CIF
            put_bits(&out, 3, 3);
        } else if (width == 704 && height == 576) {
            // 4CIF
            put_bits(&out, 3, 4);
        }
        else if (width == 1408 && height == 1152) {
            // 16CIF
            put_bits(&out, 3, 5);
        }
        else {
            // need PLUSPTYPE
            put_bits(&out, 3, 7);
            plusptype = 1;
        }

        if (!plusptype) {
            // simple header
            put_bit(&out, p_type);
            put_bits(&out, 4, 0);
        } else {
            // plusptype
            // UFEP - assume I=1 and P=0
            put_bits(&out, 3, p_type ? 0 : 1);
            if (! p_type)
            {
                // OPPTYPE
                put_bits(&out, 18, 0x30008);
            }
            // MPPTYPE
            put_bits(&out, 9, (p_type << 6) | 1);

            // CPM
            put_bit(&out, 0);
            // no PSBI

            // CPFMT if I-Frame
            if (! p_type)
            {
                // PAR (always 1:1)
                put_bits(&out, 4, 1);
                // Width
                put_bits(&out, 9, (width / 4) - 1);
                // always 1
                put_bit(&out, 1);
                // Height
                put_bits(&out, 9, (height / 4));
            }
        }

        // PQUANT
        put_bits(&out, 5, qscale);

        // CPM (if not plusptype)
        if (! plusptype)
            put_bit(&out, 0);
        // no PSBI

        /* PEI */
//        if (skip_1stop_8data_bits(&s->gb) < 0)
//            return AVERROR_INVALIDDATA;
//        break;
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
