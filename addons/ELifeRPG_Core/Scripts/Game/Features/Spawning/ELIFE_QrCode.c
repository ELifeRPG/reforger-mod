//------------------------------------------------------------------------------------------------
/*!
	QR code encoder (ISO/IEC 18004, Model 2): byte mode, ECC level M, versions 1-20, all 8 masks with the four penalty rules.
	Pure script, no UI. Encode() returns a row-major square matrix, true = dark, without quiet zone.
	Port of a Python reference: the reference was verified with zbarimg. The port is verified by Test_Qr_OwnerUrlMatrix once the self-tests run in Workbench.
	Bytes: text.ToAscii(i) returns the raw string bytes (not ISO-8859-1 decoded); data is int with & 0xFF.
*/
class ELIFE_QrCode
{
	protected const int MAX_VERSION = 20;
	protected const int MODE_BYTE = 4; // 0100
	protected const int EC_BITS_M = 0; // ECC level M is 00 in the format bits
	protected const int FORMAT_POLY = 0x537;
	protected const int FORMAT_XOR = 0x5412;
	protected const int VERSION_POLY = 0x1F25;
	protected const int GF_POLY = 0x11D;
	protected const int PAD_A = 0xEC;
	protected const int PAD_B = 0x11;

	//------------------------------------------------------------------------------------------------
	/*!
		Encodes the string as bytes. Returns false (with an empty matrix) when it does not fit version 20.
		modules is row-major with size * size entries, true = dark.
	*/
	static bool Encode(string text, out int size, out array<bool> modules)
	{
		size = 0;
		modules = new array<bool>();

		int byteCount = text.Length();
		int version = ChooseVersion(byteCount);
		if (version == 0)
			return false;

		array<int> data = new array<int>();
		for (int i = 0; i < byteCount; i++)
		{
			data.Insert(text.ToAscii(i) & 0xFF);
		}

		size = 17 + 4 * version;
		array<int> codewords = BuildCodewords(data, version);

		array<bool> baseModules = new array<bool>();
		array<bool> fn = new array<bool>();
		for (int k = 0; k < size * size; k++)
		{
			baseModules.Insert(false);
			fn.Insert(false);
		}
		BuildFunctionPatterns(version, size, baseModules, fn);
		PlaceData(baseModules, fn, size, codewords, version);

		array<bool> best = new array<bool>();
		int bestPenalty = 0;
		bool haveBest = false;
		for (int mask = 0; mask < 8; mask++)
		{
			array<bool> candidate = new array<bool>();
			for (int k = 0; k < size * size; k++)
			{
				candidate.Insert(baseModules.Get(k));
			}
			for (int r = 0; r < size; r++)
			{
				for (int c = 0; c < size; c++)
				{
					if (!fn.Get(r * size + c) && MaskCondition(mask, r, c))
						candidate.Set(r * size + c, !candidate.Get(r * size + c));
				}
			}
			PlaceFormat(candidate, size, FormatBits((EC_BITS_M << 3) | mask));
			if (version >= 7)
				PlaceVersion(candidate, size, version);

			int penalty = Penalty(candidate, size);
			if (!haveBest || penalty < bestPenalty)
			{
				haveBest = true;
				bestPenalty = penalty;
				best = candidate;
			}
		}

		modules = best;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Format info: BCH(15,5) of the 5 data bits (ECC level and mask), XORed with 0x5412.
	static int FormatBits(int formatData)
	{
		int rem = formatData;
		for (int i = 0; i < 10; i++)
		{
			rem = (rem << 1) ^ ((rem >> 9) * FORMAT_POLY);
		}
		return ((formatData << 10) | rem) ^ FORMAT_XOR;
	}

	//------------------------------------------------------------------------------------------------
	//! Version info for versions 7+: BCH(18,6).
	static int VersionBits(int version)
	{
		int rem = version;
		for (int i = 0; i < 12; i++)
		{
			rem = (rem << 1) ^ ((rem >> 11) * VERSION_POLY);
		}
		return (version << 12) | rem;
	}

	//------------------------------------------------------------------------------------------------
	//! Reed-Solomon check codewords of data for n ECC codewords over GF(256) with polynomial 0x11D.
	static array<int> ReedSolomonEcc(array<int> data, int n)
	{
		array<int> gen = RsGenerator(n);
		array<int> res = new array<int>();
		for (int z = 0; z < n; z++)
		{
			res.Insert(0);
		}

		for (int d = 0; d < data.Count(); d++)
		{
			int factor = data.Get(d) ^ res.Get(0);
			array<int> shifted = new array<int>();
			for (int s = 1; s < n; s++)
			{
				shifted.Insert(res.Get(s));
			}
			shifted.Insert(0);
			res = shifted;
			for (int i = 0; i < n; i++)
			{
				res.Set(i, res.Get(i) ^ GfMul(gen.Get(i + 1), factor));
			}
		}
		return res;
	}

	//------------------------------------------------------------------------------------------------
	//! Interleaved codewords (data then ECC, block by block) for the version. Length is the version's total codewords.
	static array<int> BuildCodewords(array<int> data, int version)
	{
		array<int> table = BlockTable();
		int base = (version - 1) * 6;
		int ecc = table.Get(base + 1);
		int g1Blocks = table.Get(base + 2);
		int g1Data = table.Get(base + 3);
		int g2Blocks = table.Get(base + 4);
		int g2Data = table.Get(base + 5);
		int blockCount = g1Blocks + g2Blocks;

		array<int> dcw = MakeDataCodewords(data, version);

		array<int> eccAll = new array<int>();
		for (int b = 0; b < blockCount; b++)
		{
			int start = BlockStart(b, g1Blocks, g1Data, g2Data);
			int length = BlockLength(b, g1Blocks, g1Data, g2Data);
			array<int> blockData = new array<int>();
			for (int i = 0; i < length; i++)
			{
				blockData.Insert(dcw.Get(start + i));
			}
			array<int> blockEcc = ReedSolomonEcc(blockData, ecc);
			for (int k = 0; k < ecc; k++)
			{
				eccAll.Insert(blockEcc.Get(k));
			}
		}

		array<int> result = new array<int>();
		int maxData = g1Data;
		if (g2Blocks > 0)
			maxData = g2Data;
		for (int i = 0; i < maxData; i++)
		{
			for (int b = 0; b < blockCount; b++)
			{
				if (i < BlockLength(b, g1Blocks, g1Data, g2Data))
					result.Insert(dcw.Get(BlockStart(b, g1Blocks, g1Data, g2Data) + i));
			}
		}
		for (int k = 0; k < ecc; k++)
		{
			for (int b = 0; b < blockCount; b++)
			{
				result.Insert(eccAll.Get(b * ecc + k));
			}
		}
		return result;
	}

	//------------------------------------------------------------------------------------------------
	//! Block layout at ECC M, 6 values per version (index (version - 1) * 6): total codewords, ECC per block, group 1 blocks, group 1 data, group 2 blocks, group 2 data.
	protected static array<int> BlockTable()
	{
		array<int> t = new array<int>();
		AddBlockRow(t, 26, 10, 1, 16, 0, 0);
		AddBlockRow(t, 44, 16, 1, 28, 0, 0);
		AddBlockRow(t, 70, 26, 1, 44, 0, 0);
		AddBlockRow(t, 100, 18, 2, 32, 0, 0);
		AddBlockRow(t, 134, 24, 2, 43, 0, 0);
		AddBlockRow(t, 172, 16, 4, 27, 0, 0);
		AddBlockRow(t, 196, 18, 4, 31, 0, 0);
		AddBlockRow(t, 242, 22, 2, 38, 2, 39);
		AddBlockRow(t, 292, 22, 3, 36, 2, 37);
		AddBlockRow(t, 346, 26, 4, 43, 1, 44);
		AddBlockRow(t, 404, 30, 1, 50, 4, 51);
		AddBlockRow(t, 466, 22, 6, 36, 2, 37);
		AddBlockRow(t, 532, 22, 8, 37, 1, 38);
		AddBlockRow(t, 581, 24, 4, 40, 5, 41);
		AddBlockRow(t, 655, 24, 5, 41, 5, 42);
		AddBlockRow(t, 733, 28, 7, 45, 3, 46);
		AddBlockRow(t, 815, 28, 10, 46, 1, 47);
		AddBlockRow(t, 901, 26, 9, 43, 4, 44);
		AddBlockRow(t, 991, 26, 3, 44, 11, 45);
		AddBlockRow(t, 1085, 26, 3, 41, 13, 42);
		return t;
	}

	//------------------------------------------------------------------------------------------------
	protected static void AddBlockRow(array<int> t, int total, int ecc, int g1Blocks, int g1Data, int g2Blocks, int g2Data)
	{
		t.Insert(total);
		t.Insert(ecc);
		t.Insert(g1Blocks);
		t.Insert(g1Data);
		t.Insert(g2Blocks);
		t.Insert(g2Data);
	}

	//------------------------------------------------------------------------------------------------
	//! Data codewords (the message capacity) of the version at ECC M.
	static int DataCodewordCount(int version)
	{
		array<int> table = BlockTable();
		int base = (version - 1) * 6;
		return table.Get(base + 2) * table.Get(base + 3) + table.Get(base + 4) * table.Get(base + 5);
	}

	//------------------------------------------------------------------------------------------------
	//! Smallest version 1-20 whose byte-mode capacity holds byteCount bytes, 0 if none does.
	protected static int ChooseVersion(int byteCount)
	{
		for (int v = 1; v <= MAX_VERSION; v++)
		{
			int countBits = 8;
			if (v >= 10)
				countBits = 16;
			if (4 + countBits + 8 * byteCount <= DataCodewordCount(v) * 8)
				return v;
		}
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Mode, count, data, terminator, byte alignment and pad bytes 0xEC/0x11 up to the data capacity.
	protected static array<int> MakeDataCodewords(array<int> data, int version)
	{
		int cap = DataCodewordCount(version) * 8;
		array<int> bits = new array<int>();
		AppendBits(bits, MODE_BYTE, 4);
		if (version <= 9)
			AppendBits(bits, data.Count(), 8);
		else
			AppendBits(bits, data.Count(), 16);
		for (int i = 0; i < data.Count(); i++)
		{
			AppendBits(bits, data.Get(i), 8);
		}

		int terminator = cap - bits.Count();
		if (terminator > 4)
			terminator = 4;
		for (int t = 0; t < terminator; t++)
		{
			bits.Insert(0);
		}
		while (bits.Count() % 8 != 0)
		{
			bits.Insert(0);
		}

		array<int> cw = new array<int>();
		for (int i = 0; i < bits.Count(); i += 8)
		{
			int value = 0;
			for (int k = 0; k < 8; k++)
			{
				value = (value << 1) | bits.Get(i + k);
			}
			cw.Insert(value);
		}

		int pad = 0;
		while (cw.Count() * 8 < cap)
		{
			if (pad % 2 == 0)
				cw.Insert(PAD_A);
			else
				cw.Insert(PAD_B);
			pad++;
		}
		return cw;
	}

	//------------------------------------------------------------------------------------------------
	protected static void AppendBits(array<int> bits, int value, int count)
	{
		for (int k = count - 1; k >= 0; k--)
		{
			bits.Insert((value >> k) & 1);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Start index of block b in the data codewords: group 1 blocks first, then group 2.
	protected static int BlockStart(int b, int g1Blocks, int g1Data, int g2Data)
	{
		if (b < g1Blocks)
			return b * g1Data;
		return g1Blocks * g1Data + (b - g1Blocks) * g2Data;
	}

	//------------------------------------------------------------------------------------------------
	protected static int BlockLength(int b, int g1Blocks, int g1Data, int g2Data)
	{
		if (b < g1Blocks)
			return g1Data;
		return g2Data;
	}

	//------------------------------------------------------------------------------------------------
	//! Generator polynomial of degree n, descending coefficients with leading 1: product of (x + a^i), i = 0..n-1.
	protected static array<int> RsGenerator(int n)
	{
		array<int> g = new array<int>();
		g.Insert(1);
		int alpha = 1;
		for (int i = 0; i < n; i++)
		{
			array<int> ng = new array<int>();
			for (int z = 0; z <= g.Count(); z++)
			{
				ng.Insert(0);
			}
			for (int j = 0; j < g.Count(); j++)
			{
				ng.Set(j, ng.Get(j) ^ g.Get(j));
				ng.Set(j + 1, ng.Get(j + 1) ^ GfMul(g.Get(j), alpha));
			}
			g = ng;
			alpha = GfMul(alpha, 2);
		}
		return g;
	}

	//------------------------------------------------------------------------------------------------
	//! Multiplication in GF(256) with reduction polynomial 0x11D.
	protected static int GfMul(int a, int b)
	{
		int x = a;
		int y = b;
		int r = 0;
		while (y > 0)
		{
			if ((y & 1) == 1)
				r = r ^ x;
			x = x << 1;
			if ((x & 0x100) != 0)
				x = x ^ GF_POLY;
			y = y >> 1;
		}
		return r;
	}

	//------------------------------------------------------------------------------------------------
	//! Alignment centres for the version: 6, the middle one for versions 7-13 (22 + 2 * (version - 7)), then 4 * version + 10.
	//! Versions 14-20 have four centres each and come from AlignmentTable.
	protected static array<int> AlignmentCentres(int version)
	{
		array<int> centres = new array<int>();
		if (version < 2)
			return centres;

		if (version >= 14)
		{
			array<int> table = AlignmentTable();
			int base = (version - 14) * 4;
			for (int k = 0; k < 4; k++)
			{
				centres.Insert(table.Get(base + k));
			}
			return centres;
		}

		centres.Insert(6);
		if (version >= 7)
			centres.Insert(22 + 2 * (version - 7));
		centres.Insert(4 * version + 10);
		return centres;
	}

	//------------------------------------------------------------------------------------------------
	//! Four alignment centres for each of versions 14-20 (index (version - 14) * 4), ISO/IEC 18004 Annex E.
	protected static array<int> AlignmentTable()
	{
		array<int> t = new array<int>();
		// version 14
		t.Insert(6);
		t.Insert(26);
		t.Insert(46);
		t.Insert(66);
		// version 15
		t.Insert(6);
		t.Insert(26);
		t.Insert(48);
		t.Insert(70);
		// version 16
		t.Insert(6);
		t.Insert(26);
		t.Insert(50);
		t.Insert(74);
		// version 17
		t.Insert(6);
		t.Insert(30);
		t.Insert(54);
		t.Insert(78);
		// version 18
		t.Insert(6);
		t.Insert(30);
		t.Insert(56);
		t.Insert(82);
		// version 19
		t.Insert(6);
		t.Insert(30);
		t.Insert(58);
		t.Insert(86);
		// version 20
		t.Insert(6);
		t.Insert(34);
		t.Insert(62);
		t.Insert(90);
		return t;
	}

	//------------------------------------------------------------------------------------------------
	//! Remainder bits after the data codewords: 7 for versions 2-6, 3 for versions 14-20, otherwise 0.
	protected static int RemainderBits(int version)
	{
		if (version >= 2 && version <= 6)
			return 7;
		if (version >= 14)
			return 3;
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Finder, separator, timing, alignment, reserved format/version areas and the dark module. fn marks every function module.
	protected static void BuildFunctionPatterns(int version, int size, array<bool> mod, array<bool> fn)
	{
		DrawFinder(mod, fn, size, 0, 0);
		DrawFinder(mod, fn, size, 0, size - 7);
		DrawFinder(mod, fn, size, size - 7, 0);

		for (int i = 8; i < size - 8; i++)
		{
			SetModule(mod, fn, size, 6, i, i % 2 == 0, true);
			SetModule(mod, fn, size, i, 6, i % 2 == 0, true);
		}

		array<int> centres = AlignmentCentres(version);
		int last = centres.Count() - 1;
		for (int ia = 0; ia < centres.Count(); ia++)
		{
			for (int ib = 0; ib < centres.Count(); ib++)
			{
				bool skip = (ia == 0 && ib == 0) || (ia == 0 && ib == last) || (ia == last && ib == 0);
				if (!skip)
				{
					int cy = centres.Get(ia);
					int cx = centres.Get(ib);
					for (int dr = -2; dr <= 2; dr++)
					{
						for (int dc = -2; dc <= 2; dc++)
						{
							SetModule(mod, fn, size, cy + dr, cx + dc, Chebyshev(dr, dc) != 1, true);
						}
					}
				}
			}
		}

		// Format info areas (values are written later per mask)
		for (int i = 0; i < 9; i++)
		{
			fn.Set(8 * size + i, true);
			fn.Set(i * size + 8, true);
		}
		for (int i = 0; i < 8; i++)
		{
			fn.Set(8 * size + (size - 1 - i), true);
			fn.Set((size - 1 - i) * size + 8, true);
		}
		SetModule(mod, fn, size, size - 8, 8, true, true);

		// Version info areas (versions 7+)
		if (version >= 7)
		{
			for (int i = 0; i < 18; i++)
			{
				int a = size - 11 + i % 3;
				int b = i / 3;
				fn.Set(b * size + a, true);
				fn.Set(a * size + b, true);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One 7x7 finder pattern with its separator, top-left corner at (row0, col0).
	protected static void DrawFinder(array<bool> mod, array<bool> fn, int size, int row0, int col0)
	{
		for (int dr = -1; dr < 8; dr++)
		{
			for (int dc = -1; dc < 8; dc++)
			{
				int r = row0 + dr;
				int c = col0 + dc;
				if (r >= 0 && r < size && c >= 0 && c < size)
				{
					int d = Chebyshev(dr - 3, dc - 3);
					SetModule(mod, fn, size, r, c, d != 2 && d <= 3, true);
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void SetModule(array<bool> mod, array<bool> fn, int size, int row, int col, bool dark, bool isFunction)
	{
		mod.Set(row * size + col, dark);
		if (isFunction)
			fn.Set(row * size + col, true);
	}

	//------------------------------------------------------------------------------------------------
	protected static int Chebyshev(int dr, int dc)
	{
		int a = dr;
		int b = dc;
		if (a < 0)
			a = -a;
		if (b < 0)
			b = -b;
		if (a > b)
			return a;
		return b;
	}

	//------------------------------------------------------------------------------------------------
	//! Writes the codewords into the free modules in the zig-zag order from the bottom-right, skipping column 6.
	protected static void PlaceData(array<bool> mod, array<bool> fn, int size, array<int> codewords, int version)
	{
		int totalBits = codewords.Count() * 8 + RemainderBits(version);
		int i = 0;
		int right = size - 1;
		while (right >= 1)
		{
			if (right == 6)
				right = 5;
			for (int vert = 0; vert < size; vert++)
			{
				for (int j = 0; j < 2; j++)
				{
					int c = right - j;
					bool upward = ((right + 1) & 2) == 0;
					int r = vert;
					if (upward)
						r = size - 1 - vert;
					if (!fn.Get(r * size + c))
					{
						bool dark = false;
						if (i < totalBits)
							dark = CodewordBit(codewords, i) == 1;
						mod.Set(r * size + c, dark);
						i++;
					}
				}
			}
			right -= 2;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Bit i of the codeword stream, MSB first in each codeword. Beyond the codewords (remainder bits) it is 0.
	protected static int CodewordBit(array<int> codewords, int index)
	{
		if (index >= codewords.Count() * 8)
			return 0;
		return (codewords.Get(index / 8) >> (7 - index % 8)) & 1;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool BitAt(int value, int i)
	{
		return ((value >> i) & 1) == 1;
	}

	//------------------------------------------------------------------------------------------------
	//! Two copies of the 15 format bits: around the top-left finder and split between the other two finders.
	protected static void PlaceFormat(array<bool> mod, int size, int bits)
	{
		for (int i = 0; i < 6; i++)
		{
			SetCell(mod, size, 8, i, BitAt(bits, i));
		}
		SetCell(mod, size, 8, 7, BitAt(bits, 6));
		SetCell(mod, size, 8, 8, BitAt(bits, 7));
		SetCell(mod, size, 7, 8, BitAt(bits, 8));
		for (int i = 9; i < 15; i++)
		{
			SetCell(mod, size, 14 - i, 8, BitAt(bits, i));
		}
		for (int i = 0; i < 8; i++)
		{
			SetCell(mod, size, size - 1 - i, 8, BitAt(bits, i));
		}
		for (int i = 8; i < 15; i++)
		{
			SetCell(mod, size, 8, size - 15 + i, BitAt(bits, i));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Both 6x3 version blocks (versions 7+).
	protected static void PlaceVersion(array<bool> mod, int size, int version)
	{
		int bits = VersionBits(version);
		for (int i = 0; i < 18; i++)
		{
			int a = size - 11 + i % 3;
			int b = i / 3;
			SetCell(mod, size, a, b, BitAt(bits, i));
			SetCell(mod, size, b, a, BitAt(bits, i));
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void SetCell(array<bool> mod, int size, int col, int row, bool dark)
	{
		mod.Set(row * size + col, dark);
	}

	//------------------------------------------------------------------------------------------------
	//! Mask condition for (row i, column j); the mask flips the data modules where it is true.
	protected static bool MaskCondition(int mask, int i, int j)
	{
		if (mask == 0)
			return (i + j) % 2 == 0;
		if (mask == 1)
			return i % 2 == 0;
		if (mask == 2)
			return j % 3 == 0;
		if (mask == 3)
			return (i + j) % 3 == 0;
		if (mask == 4)
			return (i / 2 + j / 3) % 2 == 0;
		if (mask == 5)
			return ((i * j) % 2 + (i * j) % 3) == 0;
		if (mask == 6)
			return (((i * j) % 2 + (i * j) % 3) % 2) == 0;
		return (((i + j) % 2 + (i * j) % 3) % 2) == 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Penalty rules N1-N4 of ISO/IEC 18004 over the whole matrix; the mask with the lowest value wins.
	protected static int Penalty(array<bool> mod, int size)
	{
		int p = 0;
		array<bool> line = new array<bool>();
		for (int r = 0; r < size; r++)
		{
			line.Clear();
			for (int c = 0; c < size; c++)
			{
				line.Insert(mod.Get(r * size + c));
			}
			p += RunPenalty(line) + FinderPenalty(line);
		}
		for (int c = 0; c < size; c++)
		{
			line.Clear();
			for (int r = 0; r < size; r++)
			{
				line.Insert(mod.Get(r * size + c));
			}
			p += RunPenalty(line) + FinderPenalty(line);
		}

		for (int r = 0; r < size - 1; r++)
		{
			for (int c = 0; c < size - 1; c++)
			{
				bool v = mod.Get(r * size + c);
				if (v == mod.Get(r * size + c + 1) && v == mod.Get((r + 1) * size + c) && v == mod.Get((r + 1) * size + c + 1))
					p += 3;
			}
		}

		int dark = 0;
		for (int k = 0; k < size * size; k++)
		{
			if (mod.Get(k))
				dark++;
		}
		int total = size * size;
		int diff = dark * 20 - total * 10;
		if (diff < 0)
			diff = -diff;
		p += 10 * (diff / total);
		return p;
	}

	//------------------------------------------------------------------------------------------------
	//! N1: runs of five or more equal modules, 3 + (length - 5) each.
	protected static int RunPenalty(array<bool> line)
	{
		int p = 0;
		int run = 1;
		int n = line.Count();
		for (int i = 1; i < n; i++)
		{
			if (line.Get(i) == line.Get(i - 1))
			{
				run++;
			}
			else
			{
				if (run >= 5)
					p += 3 + (run - 5);
				run = 1;
			}
		}
		if (run >= 5)
			p += 3 + (run - 5);
		return p;
	}

	//------------------------------------------------------------------------------------------------
	//! N3: 1011101 with four light modules on one side (10111010000 or 00001011101 in the line), 40 each.
	protected static int FinderPenalty(array<bool> line)
	{
		int p = 0;
		int n = line.Count();
		for (int i = 0; i + 11 <= n; i++)
		{
			int w = 0;
			for (int k = 0; k < 11; k++)
			{
				w = (w << 1) | BoolInt(line.Get(i + k));
			}
			if (w == 0x5D0 || w == 0x05D)
				p += 40;
		}
		return p;
	}

	//------------------------------------------------------------------------------------------------
	protected static int BoolInt(bool value)
	{
		if (value)
			return 1;
		return 0;
	}
}
