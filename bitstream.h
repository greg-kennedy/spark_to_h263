#pragma once

// bitstream structs
struct bitstream
{
    FILE* fp;
    unsigned char byte;
    unsigned char mask;
};

static int get_bit(struct bitstream* bs)
{
    // gets the next bit of FP
    if (!bs->mask)
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
    printf("%d", retval);
    bs->mask >>= 1;
    return retval;
}

static int get_bits(struct bitstream* bs, int count)
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