.HTML "Modern Unicode Requires Modern Solutions"
.TL
Modern Unicode Requires Modern Solutions
.AU
Jacob Moody
.AB
Plan 9 has a history with Unicode, the authors of which were
directly involved with the creation and first system-wide
implementation of UTF-8. This should come as no surprise with the
focus on plaintext that permeated UNIX, and how that focus had carried
over into Plan 9. However, the world around Unicode has changed much
since its early versions, becoming more complex and detail
oriented as the version number climbs higher. While Plan 9 from Bell
Labs was never updated to any major version of Unicode beyond the first,
9front has made incremental progress over the years in adopting newer
standards. We aim to evaluate the current state of the union with
Unicode support, and different natural languages at large, within the
current 9front system, both where the system is at now and where we
would like it to go.
.AE
.EQ
delim $$
.EN
.SH
Background
.PP
.FS
First presented in a slightly different form at the 10th International Workshop on Plan 9.
.FE
.LP
Computers do not handle abstract notions such as natural
language particularly well, so we rely on standards for representing natural
language as a sequence of numbers. The mapping of
numbers to natural language characters is known as a character
encoding. While there have been many character encodings
invented and used over the years, the one that has seen the most
widespread adoption now is Unicode. Unicode is unique among other
character encodings before it in the fact that it plans to have a
single encoding for all natural languages, done in such a way that a
single document could contain language samples from all natural
languages without out of band code page switching or the like (as was
common before Unicode).
.LP
With Unicode's goal of serving every natural language and the
vast differences between natural languages there comes a need for
careful definition of terms that the standard uses[Glossary].
To ensure some clarity let us get some of these definitions out in front:
.TS
l l
^ ^
l l
l l
l l
^ ^.
Abstract Character	A unit of information used for the organization, control,
	or representation of textual data.
Codepoint	Any value within the Unicode codespace. In convention with
	traditional plan 9 terminology, we use rune as a synonym for codepoint.
Encoded Character	A codepoint which has been mapped to an abstract character
Grapheme	A minimally distinctive unit of writing in the context of a particular
	writing system
Glyph	The actual, concrete image of a glyph representation having been
	rasterized or otherwise imaged onto some display surface.
.TE
.LP
With that being said, let us define how these terms relate. Broadly
abstract characters are information that we wish to encode in the
character encoding, this is everything from graphemes, to grammar
markings, to components of a letter, to informational control
characters. Unicode gives us a large bit space to work with, so a
codepoint is a single index into that bitspace, the standard then
gives us the mapping for relating these codepoints to abstract
characters. A large majority (but not all) of encoded characters
represent graphemes. Graphemes are just one possible category
that an encoded character can be within. Unicode only really defines these
encoded characters, the rendering and presentation of these encoded
characters is the responsibility of the rendering layer who takes
encoded characters as input and outputs glyph images.
.LP
The code space of Unicode is currently 21 bits, and with computing
hardware mostly rounding that up to 32 bits, that can be a non-trivial
amount of storage for large amounts of text. For this the Unicode
standard also defines a few compact encoding schemes, the most
popular of which is
.CW UTF-8 ,
encoding the Unicode bit space within a variable number of bytes up to four.
.LP
Unicode also provides multiple codepoints (or a sequence of
codepoints) for representing the same abstract character. A large
chunk of these characters are those which have been identified as
being composed out of two or more subcomponents. The most common
example of this are characters that have diacritical markings, which
can either be represented as one codepoint for the "precomposed"
variant or as a series of codepoints with the base codepoint first
followed by codepoints which denote individual diacritical markings.
.LP
Considering that there are multiple different representations of the
same abstract character, there is a method to normalize a series of
codepoints into a consistent representation for semantic comparisons.
The Unicode standard[UAX#15] does define a process for conducting this
normalization, and provides 4 different forms to normalize to:
.TS
l l.
NFC	Precomposed codepoints
NFD	Decomposed codepoints
NFKC	Precomposed compatibility codepoints
NFKD	Decomposed compatibility codepoints
.TE
.LP
The compatibility forms here do additional conversion of special
formatted character sets such as codepoints for specific fonts,
rotated variants, and so on into their base
counterparts. This operation loses some information regarding the
original codepoints thus is separated from typical normalization. There
are some additional rules that are part of normalization so as to
enable checking the semantic equivalence between any two sequence of codepoints.
.SH
Existing Unicode in Plan 9
.LP
Plan 9 was an early adopter of Unicode, but never reached full
compliance for any standard version of Unicode during its history.
Pike et al put in a lot of work for handling UTF-8
consistently within the system[Pike93] and a majority of the system was
updated to handle codepoints. Plan 9's initial support could
be defined succinctly as supporting "21-bit ASCII encoded as \f(CWUTF-8\fR",
where each codepoint is assumed to be exactly one abstract
character and interfaces broadly assume that mutations to text (ie
backspace in a text editor and the like) operate only on single code
points as well. The result of this is that the system can only really
handle precomposed Unicode in any sort of ergonomic way, however Plan
9 is very consistent in this level of support and has been ever since its
last official release.
.LP
Plan 9 had initially only been written to support Unicode version 1.0
(16-bit code space), so one of the earliest contributions from the community
was to update this to the modern 21-bit code space. This work was first
done in 9atom[9atom], and was later done in 9front. This includes
work to make more of the system Unicode aware that wasn't
previously. The version of awk which ships with the system is a notable
example. There were also some smaller changes like updates to the
input "compose" sequences to allow a wider range of codepoints and
allowing users to manually input them by typing out their
direct value in hexadecimal. However even with that work there
have been portions of 9front that had yet to still be updated past
Unicode version 1.0.
.SH
Using Tables to Keep Pace
.LP
The work being presented in this paper largely started with wanting to
update some of the existing interfaces within the system, namely the
.CW isalpharune (2)
set of functions, in order to not only bring them up to date,
but also allow their adaptation to future versions of the standards.
To that end, the Unicode consortium publishes a
series of informational databases[UAX#44] in an awk-friendly format. Plan 9 users
may be familiar with one of these data files as the original
.CW /lib/unicode
was a cut-down and reformatted version of the Unicode
Character Database (UnicodeData.txt). These files are updated and
maintained as the Unicode versions change, so the natural course of
action seemed to be to read these files as input to generate code that
could be then used to implement whatever functions we may need.
.LP
To better illustrate the input data, below is an excerpt of UnicodeData.txt
for U+2468. Each line of UnicodeData.txt corresponds to a single codepoint,
with various properties of said codepoint separated by semicolons. Both
the keys and values for each field are documented by the Unicode standard[UAX#44].
.P1
; look -b 16 2468 /lib/ucd/UnicodeData.txt 
2468;CIRCLED DIGIT NINE;No;0;ON;<circle> 0039;;9;9;N;;;;;
.P2
.LP
The parsing of these text input files is largely unremarkable, we read
in one line at a time and denote properties and conversions as we move
ourselves along. The challenge comes in picking and generating a
reasonable data structure for the function implementations to use,
balancing both the size of the data structure in memory along with the
runtime complexity for walking the data structure. Plan 9 binaries
are currently quite small in comparison to the hardware available, so
we had decided to err on the side of increasing the binaries size
in exchange for better performance.
Because of this we decided to implement direct lookup
tables, using codepoints as an index and having an O(1) lookup time
for the vast majority of keys.
.LP
With that said, multiple arrays each with
$2 sup 21$
entries is quite
untenable even given the potential performance savings. To mitigate
this we can use properties of the distribution of abstract characters
over the Unicode code space to our advantage. Unicode code space is
distributed up into chunks, with natural language roughly being
contained within their own contiguous range of codepoints. This
means that for many of the properties we care about there are hotspots
and coldspots throughout the code space. As an example, you could
consider the
.CW toupperrune ()
function, which largely is only applicable
to natural languages which feature distinct letter cases. Many
eastern Asian languages have no concept of casing so those regions
of the Unicode code space don't need defined values. This makes
the lookup table quite sparse, and therefore a good candidate
for some method of compression. This is not unique to letter
casing, and in fact all of the lookup tables for
.CW isalpharune (2)
end up being sparse tables.
.LP
To compress the lookup table, we use a data structure that [Gillam02]
referred to as a compact array. We start by breaking up the lookup
table into equally sized segments, then walk those segments and find
any overlap (full or partial) between them. We can then remove any of
the overlapped entries within the table. In order to access these
overlapped segments we then need a higher level lookup table which
points into these segments of the lower table. In order to use a
single key (a codepoint in our case) for indexing in to both tables,
we split up the bits within the key into two different portions, the
top bits indexing in to the top table, the result of which is added to
the bottom bits in order to index in to the bottom table.
.LP
Given how large the code space is for Unicode, it becomes beneficial
to do not just one layer of compacting, but two, where the higher
lookup table is compacted with the same method, resulting in a new top
level lookup table. This splits up the codepoint into three parts,
the result of the top level table being used to index the
middle table which is then used to index the bottom table which
contains our desired value. The following code illustrates an example
of this lookup method:
.P1
#define upperindex1(x) (((x)>>(4+7))&0x3FF)
#define upperindex2(x) (((x)>>4)&0x7F)
#define upperoffset(x) ((x)&0xF)
#define upperlkup(x) (_upperdata[_upperidx2[_upperidx1[upperindex1(x)]
	+ upperindex2(x)] + upperoffset(x)])
.P2
In this example, the top ten bits of the codepoint index the top level
index, the result of which is combined with the next 7 bits of the
codepoint, with that result of that being added to the remaining 4
bits of the codepoint. Which amount of bits to use for each section is
not exactly clear, so we brute forced the smallest table size through
testing all possible values.
.LP
Of course in order for this to be effective we need to ensure that the
values of our lookup table are sparse. For simple property values this
is typically already the case, and for functions like
.CW toupperrune ()
we can instead store a delta between the current rune and the desired
rune, which gives us nice runs of zero values for anything without a
defined transformation. Using this method we've gotten the following
results for the
.CW isalpharune (2)
set of functions:
.TS
l l
l n.
Table	Size (bytes)
toupper	12436
tolower	11754
totitle	12180
is*rune	24184
total	60554
.TE
.LP
The
.CW is*rune ()
table here merges all of the different categories into
one table as a bit field, one bit per possible category, so the result
is less sparse than the other tables. We find the expense of 60KB to
be well worth the benefits gained from the ease of upgrade, and
constant time lookup speed.
.SH
Keeping Composure
.LP
Recall that normalization is the process of transforming a series of
Unicode codepoints into a canonical variant for a given target format.
In order to do this, we'll need tables that map individual codepoints
to their transformed values for each format, their composed or decomposed
variants. We can use tables largely similar to the ones described above, with
some extras where needed to handle more specific edge cases.
.LP
For decomposition our lookup key is the same as before, the codepoint
of the rune we wish to decompose. However the value of the table gets
a bit more complex. The most straightforward approach would be to
combine the two new runes as one value, but this is fairly painful
for the table size. We'd need a 42-bit value, which would have to be
rounded up to 64. Not only are we wasting over a third of the storage
space due to the rounding, but using 64-bit values would be quite
onerous for our 32-bit systems. As it turns out however, there is a
heavy bias for the new decomposed runes to reside within the lower 16
bit code space. In the latest version of Unicode at the time of
writing (16.0) there are only 158 runes that result in decompositions
above the 16-bit code space. It becomes easiest then to have our
compact array use 32-bit values (two 16-bit codepoints), and then
have an exceptions table which stores those 158 runes.
The base table values for these exception runes are instead given
within a small portion of the Unicode private space (0xEEEE on up),
with the difference from the start of the exception being used to index the list of exceptions.
This allows us to maintain our constant time lookups, even when
accessing members of the exception range.
.LP
Recomposition required a different approach with similar constraints
as the decomposition. Instead of packing two runes into the output of
the table we instead need to pack two runes into the input of our
lookup, and produce one rune as output. The bias towards the 16-bit
code space remains, so we can limit our main lookup table to taking
two 16-bit runes as input and outputting a single 16-bit rune.
For exceptions, (where either the input runes or output runes exceed 16 bits)
we use a similar exception table. However in this case we have to search this
linearly because otherwise we would need to increase the table size to accomodate
the larger lookup keys. The differences from decomposition make it much harder to make use of
our existing lookup table, so we instead opted to implement a hash
table.
.LP
A hash table is going to have a worse average lookup time compared to
our compact arrays (assuming moderate collision), but stays within the
same ballpark of performance and size as our compact array. Because
we're generating this table at build time instead of at run time, we have
to find a way of embedding the entire structure in a contiguous block
of memory. This is easy to accomplish for our first result of the
hash function, where we have one large chunk of memory: we hash our
input key then modulo it by the size of our initial memory buffer.
For collisions however we need to find some way of chaining the
results in a way that can be searched until a key matches. For this we
create another array that is appended onto the first.
The main entry points are chained to the
collisions by embedding a numeric offset from the start of the
collision space. This results in a hash table entry that looks like the
following:
.P1
struct KV {
	uint key;
	ushort val;
	ushort next;
};
.P2
In the interest of saving as much space as possible for the generated
array, we can compact this struct into a series of paired integers, the
first being the merger of the two 16-bit codepoints, and the second
being the merger of the value and optionally the offset to the collision space.
.LP
As with choosing the constants for compact arrays, we found an
optimal size for the original hash array versus the collision array
largely through brute force. Given the Unicode standard at the time
of writing, we are using 512 initial buckets, which forces 555 entries
into the collision array. With a hash function that provides a
reasonable equal distribution, this should average to roughly only one
collision per entry. Using these two data structures results in
the following generated table sizes:
.TS
l l
l n.
Table	Size (bytes)
decomp	20108
recomp	8536
.TE
.SH
Normalizing Theory
.LP
With composition handled we can then start to tackle normalizing a
string to one form or the other. The implementation of Unicode
normalization can be quite finicky and there are plenty of
considerations for how the input must be fed to result in conformant
output. However before we can get into those, we need some additional
tables that will be used in the normalization process.
.LP
The first additional table we need is one that stores the
.I "Combining Character Class"
.CW (ccc)
of a codepoint, this is, the value which identifies
the attaching property of the character. A value of non-zero
signifies that the codepoint is an attacher, the specific value
denoting the group of attachers that it belongs to.
Typically only one attacher out of each group may be considered
for attachment to a base codepoint.
The ccc value is also used to ensure the ordering of attaching
codepoints is consistent in
.CW NFD
formatted strings[UAX#15].
The other value we need is the Quick Check (qc)
value of a codepoint which is used to determine where a normalization
context ends in certain situations. We'll elaborate on this further
in the following section. Our existing compact
array structure works well enough for these lookup tables as is,
resulting in the following sizes:
.TS
l l
l n.
Table	Size (bytes)
ccc	9738
qc	7878
.TE
.LP
The abstract logic for normalization is described in [UAX#15] and is
as follows:
.P1
1. Decompose every rune possible in the string until it
   can no longer be decomposed
2. For codepoints without a ccc of zero, conduct a
   stable sort of them based on their ccc value
3. If we are normalizing to NFC, recursively recompose code
   points in the string until there is nothing more to recompose
.P2
.LP
The main challenge for normalization then is designing an algorithm
that can do all of this on a single pass of the string. To accomplish
this, we iterate through a string and accumulate runes into a context.
A context conceptually aims to localize the effect of normalization, breaking
it down into chunks, such that a chunk can be processed independantly.
This allows very large strings to be processed piecewise and within
reasonable memory boundaries. We implemented the context by using a stack,
which is cleared and reused for multiple contexts as we parse through the string.
The process by which we construct the first part of our context is as follows:
.P1
1. Take the next rune from the string and push it onto our stack
2. If the following rune has a ccc not equal to zero,
   push it on the stack. Repeat until we encounter a rune with a
   ccc of zero.
3. Perform a stable sort of the stack by the ccc value of the codepoints
.P2
.LP
At this point if we are normalizing to a decomposed form (NFD) our context
is finished and may be flushed. Continuing the process again for the next
rune in the string.
.LP
If we're recomposing, we now must classify which type of context that we have
before we continue.  

Here we split into one of two recomposition strategies based on which
type of context we have. Strategy A is used for sequences of codepoints
which have attachers (ccc of non-zero), and strategy B for sequences
of composable base runes (ccc of zero).
.LP
.P1
ccc of non zero/Strategy A
1.  Use the base codepoint as the left hand side input to our
    recomposition
2.  Use the next immediate codepoint as the right hand side input
    to our recomposition.
2a. If a match is found, replace the base rune with the new result
    and discard the right hand size codepoint. The new codepoint will
    be used as the base codepoint for further iterations.
2b. If a match isn't found, skip through all other codepoints
    in the stack that have the same ccc as the failed match.
3.  Return to step 1 with and continue to attempt matches with
    remaining codepoints in the stack until the stack is depleted.
.P2
.LP
.P1
ccc of zero/Strategy B
1.  Continue to pull codepoints on to the stack from the input
    string until we reach a boundary, as described by the
    Quick Check value.
2.  Walk through each codepoint in the stack, attempting to
    compose it with the codepoint immediately to the right of it.
2a. If a match is found, replace the left hand side codepoint with
    the result, and discard the right hand side codepoint.
2b. If you don't find a match, continue through the stack with
    the new left hand side being the current right hand side.
3.  Go back to step 1 if any recomposition was found until we make a
    pass without any recompositions.
.P2
.LP
After either of these two steps the resulting stack can be flushed out
as normalized output. The stipulation to bypass other runes which have
the same ccc (step 2b in strategy A) is stipulated by the standard[UAX#15]
and required for a conformant implementation.
.LP
There does exist an edge case in normalization, as the standard itself
puts no limit on the amount of attaching codepoints that may be given to a
base codepoint. This can be seen in what is typically
referred to as "zalgo text", where a single base codepoint is given
a large amount of attachers to produce a "glitchy" appearence.
While there is no natural language which
relies on a large number of attaching codepoints, it remains an edge
case our algorithm must come to terms with. In order to perform this
normalization within a bounded memory constraint we need to pick some
reasonable maximum amount of attaching codepoints that we plan to support.
Unicode defined a subset of normalized text referred to as
"Stream-Safe text"[UAX#15] which dictates that no more than 30 attaching code
points may be given to a base codepoint, and for sequences larger than that,
attaching codepoints can be broken up with
.CW U+034F
(Combining Grapheme Joiner). We implement this recommendation, which means our
algorithm will insert a CGJ into a text stream if we encounter large
enough runs. This is required if we want to output properly normalized
text, as without it the ccc context would grow to a length that would
be untenable to sort. The CGJ acts as a new base, and codepoints
which attach to it can be sorted independently of those on the base
codepoint. The rendering layer should be able to interpret the
attaching codepoints on the CGJ as part of the previous base code
point.
.SH
Normalization Interface
.LP
With the general algorithm for normalization figured out we need to
decide on an interface for exposing this to other programs. Due to the
nature of normalization this can be somewhat complicated to do well.
As illustrated earlier, context expansion in both types of context
continues until we find the beginning of the next context. In strategy
A it is until we find a ccc of zero and in strategy B it is until we
find qc boundary. This means that for most normalization we need to
know when an input ends in order to flush the last context, since
there is no next context to act as our terminator. This is relatively
easy if we're operating on the entire string at once: we can walk
until we reach the end of the input string and use that as a sign to
flush whatever remaining context we may be holding on to. If we're
dealing with a stream of input, from a file/pipe/network connection,
then we may wish to normalize one chunk at a time. We then need to maintain
the remaining context of one chunk into the beginning of the next
chunk. Other implementations handle this issue by implementing a
concatenation function, which carefully conjoins one normalized string
to the other with only contextualizing the edges. We decided to
instead implement only a streaming interface, which can be used for
both concatenation and streaming input. While pushing two strings
that had been independently normalized back through the streaming interface
is not as efficient as only looking at the boundaries, we think that
the normalization implementation itself should be sufficiently fast to
have a negligible performance impact in a majority of cases.
.LP
The following streaming interface is what we had settled on:
.P1
typedef struct Norm Norm;
struct Norm {
	... /* private */
};
extern void norminit(Norm*, int comp, void *ctx, long (*get)(void*));
extern long normpull(Norm*, Rune *dst, long sz, int flush);
.P2
.LP
In addition we provide the following wrapper functions which target
normalization for smaller fixed set inputs:
.P1
extern long runecomp(Rune *dst, long ndst, Rune *src, long nsrc);
extern long runedecomp(Rune *dst, long ndst, Rune *src, long nsrc);
extern long utfcomp(char *dst, long ndst, char *src, long nsrc);
extern long utfdecomp(char *dst, long ndst, char *src, long nsrc);
.P2
.LP
With the streaming interface: the
.CW flush
flag in
.CW normpull ()
specifies whether we should preserve the remaining context once we reach an EOF from a
.CW get ()
function. This allows callers to normalize fixed chunks
at a time, then call with the flush argument set to non-zero when
there is no more input to give. The
.CW ctx
is a caller supplied pointer which is also passed forward on to the
.CW get ()
function. If
.CW Comp
is non-zero the string is composed, meaning the output is
.CW NFC
instead of
.CW NFD .
.LP
The Unicode standard publishes an extensive set of tests[UAX#44] to ensure
that normalization implementations deal with all of the possible edge
cases which show up within expected realistic input. We verified
our implementation against this series of tests, and added support
for normalized strings to
.CW tcs (1).
.SH
Preparation and Future Work
.LP
While normalization itself is not within the crucial path to
generating a better conforming Unicode interface, the necessary infrastructure
for implementing normalization provides the foundation for implementing more comformant
interfaces.
For this paper we want to focus on what we think is
the next step, which is a more conformant interface for Plan 9
programs which use
.CW frame(2).
This would be
.CW rio (1),
.CW acme (1),
and
.CW samterm (1).
.LP
As implemented today, these programs assume that each codepoint is its
own abstract character and extra attaching codepoints are treated as
individual, separate characters. Instead of attaching combining
characters to their base codepoint rio, sam and friends will render
them on their own, resulting in a dangling diacritic in the best cases
and a stray PJW in the worst. This results in decomposed text being
largely unusable within these interfaces. If we would like these
programs to handle such text better, we need interfaces that allow it
to find logical breaks within the text. Unicode defines[UAX#29] two
types of breaks, grapheme breaks and word breaks.
.LP
Grapheme breaks are boundaries between codepoints in which a user may
consider it to be one logical abstract character. The codepoints
between these breaks (referred to as a grapheme cluster)
should be considered as one abstract character by editors.
For example, an editor may choose to interpret a backspace to be the
deletion of codepoints up until the previous grapheme break. Likewise
an editor should not put a line break within a grapheme cluster.
Another consideration may be whether
.CW sam (1)'s
structured regular expression should be changed to interpret grapheme
clusters as one logical character or as independent codepoints.
.LP
Word breaks are similar to grapheme breaks, but for exclusively
finding where in a series of codepoints an interface may choose to
place a line break. This prevents cases like placing a line break
halfway through a series of numerics, or in general places which might
confuse a native speaker when reading the text. Of course it is not
always possible to abide by these rules, as the width that an editor is
working with may be shorter than the full context of a word break
(consider a file full of 1000 1's), so at best it serves as an
optimistic placement. While nice to have for text editors, we
believe that a more important application lies in typesetting documents.
.LP
The algorithms for finding these breaks are less involved than
normalization, and we were able to again use our compact arrays to
store the required lookup information for codepoints. As part of this
paper we implement the following set of functions for finding grapheme
and word breaks within an input string:
.P1
Rune*  runegbreak(Rune *s);
Rune*  runewbreak(Rune *s);
char*  utfgbreak(char *s);
char*  utfwbreak(char *s);
.P2
The return values of these functions point to the beginning of the
next context.
.LP
Of course these functions are only the start of implementing proper
support in our editors and interfaces. Future work may look to
reimplement
.CW frame (2)
to make use of these functions for implementing editors that work
well in languages other than English. We have so far also
largely ignored the rendering portions of these interfaces. It is
another important step required for conformance but the rules for
rendering extends beyond what is specified in Unicode and is best
saved for a future paper which explores the full complexity of that
problem space.
.SH
Acknowledgements
.LP
Special thanks to Ori Bernstein, cinap_lenrek, and the rest of the
9front community for their support in designing and thinking about
this problem space. The work as it is presented would have not been
possible without their advice and review.
.SH
References
.LP
[Gillam02]
Richard Gillam,
``Unicode Demystified: A Practical Programmer's Guide to the Encoding Standard'',
.IR "Addison-Wesley Professional" ,
January,
2002
.LP
[Glossary]
Unicode Inc,
Glossary of Unicode Terms,
https://www.unicode.org/glossary/,
.LP
[Pike93]
Rob Pike, Ken Thompson,
``Hello World or Καλημέρα κόσμε or \f(Jpこんにちは 世界\fP'',
January,
1993
.LP
[UAX#15]
Unicode Inc,
Unicode Normalization Forms,
https://www.unicode.org/reports/tr15/,
Version 16.0.0,
August,
2024
.LP
[UAX#44]
Unicode Inc,
Unicode Character Database,
https://www.unicode.org/reports/tr44/,
Version 16.0.0,
August,
2024
.LP
[UAX#29]
Unicode Inc,
Unicode Text Segmentation,
https://www.unicode.org/reports/tr29/,
Version 16.0.0,
August,
2024
.LP
[9atom]
Eric Quanstro,
http://www.9atom.org/,
https://web.archive.org/web/20201111224911/http://www.9atom.org/,
November,
2020
