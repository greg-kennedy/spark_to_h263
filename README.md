# spark_to_h263
Losslessly convert Sorenson Spark bitstreams to ITU-T H.263

## Overview
[H.263](https://en.wikipedia.org/wiki/H.263) is a video compression standard (codec) first published March 20, 1996 by ITU-T.  It was used widely for videoconferencing over slow / lossy transmission lines (phone, ISDN) and multiple implementations exist.

[Sorenson Spark](https://en.wikipedia.org/wiki/Sorenson_Spark) is an implementation of H.263 by Sorenson Media.  It contains a handful of features that extend H.263 in incompatible ways.  It gained prominence as the first video codec supported by Adobe Flash Video, made available March 15, 2002 in version 6.  Spark remained the only codec available until Flash version 8 (September 13, 2005) when On2 VP6 was introduced.

As a result, there are a number of Spark video files available in SWF or FLV containers.  These streams are not portable to other H.263 decoders or containers (e.g. 3GPP, MP4).

In February 1998 the ITU-T group issued an update to H.263 called "H.263v2" or "H.263 Plus".  This added a number of optional enhancements to H.263 streams, adding flexibility of the codec beyond its original use for video conferencing.  Interestingly, H.263+ is comprehensive enough to cover all extensions made in Sorenson Spark.

Thus, it should be possible to convert a Spark bitstream to an H.263+ bitstream with no need to transcode, and no quality loss.  Hence, this tool.

## Details
The specific changes between H.263, Spark, and H.263+ are described in the following table.

It will be helpful to have a copy of the [ITU-T H.263 recommendation](https://www.itu.int/rec/T-REC-H.263/recommendation.asp?lang=en&parent=T-REC-H.263-200501-I) document.

| Feature | H.263 | Spark | H.263+ |
| ------- | ----- | ----- | ------ |
| Resolution | Limited to xCIF standard (128x96, 176x144, 352x288, 704x576, 1408x1152) | Arbitrary | Arbitrary (multiple of 4), see 5.1.5 CPFMT |
| Framerate  | 29.97 (30000/1001) fps | Not stored, see FLV container | Arbitrary, see 5.1.7 CPCFC |
| Pixel Aspect | 11:12 | Not stored, see FLV container | Arbitrary, see 5.1.5 CPFMT |
| Frame Types | I, P | I, P, Droppable P | I, P |
| Picture Header | H.263 | Spark | H.263 or H.263+, see 5.1.4 Plus PTYPE |
| IDCT Coefficient Range | -127 to 127 | Extended | Extended, see T.4 Modified Coefficient Range |
| IDCT Encoding | Table | Table + 11-bit Escape Code | Table + Annex T escape, see T.4 Modified Coefficient Range |

In other words, three main steps need to be taken to translate Spark into H.263+
* The picture header must be rewritten from Spark into H.263+.  This adds additional fields to accomodate Spark features of resolution, framerate, pixel aspect.
* IDCT coefficients outside the range -127 to 127 are not contained in the default H.263 Huffman code table and must be stored "literally" in the bitstream.  Spark and H.263+ both support this, but differ in the escape code used and the representation of the bits in the file.  This must be converted.

The final Spark feature missing is "Droppable P-Frames": these are intermediate frames that can be safely skipped by a decoder that is lagging behind, because no subsequent P-Frames reference these.  Examples would be the final P-frame before an I-frame (because the screen will be repainted from scratch anyway), or a P-frame which updates screen regions that are not touched again until the next I-frame.  H.263 does not support this; however, we simply treat all frames as "not droppable" and encode them as regular P-frames.

libavcodec source indicates that Spark includes a flag for a "deblocking filter", but it is skipped.  H.263+ also includes a deblocking filter, but because there are no Spark files that actually use it, it is omitted here.