#pragma once
/**
	@file
	@brief optimal ate pairing
	@author MITSUNARI Shigeo(@herumi)
	@license modified new BSD license
	http://opensource.org/licenses/BSD-3-Clause
*/
#include <mcl/fp_tower.hpp>
#include <mcl/ec.hpp>
#include <assert.h>

namespace mcl { namespace bn {

struct CurveParam {
	/*
		y^2 = x^3 + b
		i^2 = -1
		xi = xi_a + i
		v^3 = xi
		w^2 = v
	*/
	int64_t z;
	int b; // y^2 = x^3 + b
	int xi_a; // xi = xi_a + i
	bool operator==(const CurveParam& rhs) const { return z == rhs.z && b == rhs.b && xi_a == rhs.xi_a; }
	bool operator!=(const CurveParam& rhs) const { return !operator==(rhs); }
};

const CurveParam CurveSNARK1 = { 4965661367192848881, 3, 9 };
const CurveParam CurveSNARK2 = { 4965661367192848881, 82, 9 };
const CurveParam CurveFp254BNb = { -((1LL << 62) + (1LL << 55) + (1LL << 0)), 2, 1 };

template<class Vec>
void convertToBinary(Vec& v, const mpz_class& x)
{
	const size_t len = mcl::gmp::getBitSize(x);
	v.clear();
	for (size_t i = 0; i < len; i++) {
		v.push_back(mcl::gmp::testBit(x, len - 1 - i) ? 1 : 0);
	}
}

template<class Vec>
size_t getContinuousVal(const Vec& v, size_t pos, int val)
{
	while (pos >= 2) {
		if (v[pos] != val) break;
		pos--;
	}
	return pos;
}

template<class Vec>
void convertToNAF(Vec& v, const Vec& in)
{
	v = in;
	size_t pos = v.size() - 1;
	for (;;) {
		size_t p = getContinuousVal(v, pos, 0);
		if (p == 1) return;
		assert(v[p] == 1);
		size_t q = getContinuousVal(v, p, 1);
		if (q == 1) return;
		assert(v[q] == 0);
		if (p - q <= 1) {
			pos = p - 1;
			continue;
		}
		v[q] = 1;
		for (size_t i = q + 1; i < p; i++) {
			v[i] = 0;
		}
		v[p] = -1;
		pos = q;
	}
}

template<class Vec>
size_t getNumOfNonZeroElement(const Vec& v)
{
	size_t w = 0;
	for (size_t i = 0; i < v.size(); i++) {
		if (v[i]) w++;
	}
	return w;
}

/*
	compute a repl of x which has smaller Hamming weights.
	return true if naf is selected
*/
template<class Vec>
bool getGoodRepl(Vec& v, const mpz_class& x)
{
	Vec bin;
	convertToBinary(bin, x);
	Vec naf;
	convertToNAF(naf, bin);
	const size_t binW = getNumOfNonZeroElement(bin);
	const size_t nafW = getNumOfNonZeroElement(naf);
	if (nafW < binW) {
		v.swap(naf);
		return true;
	} else {
		v.swap(bin);
		return false;
	}
}

template<class Fp>
struct ParamT {
	typedef Fp2T<Fp> Fp2;
	typedef mcl::EcT<Fp> G1;
	typedef mcl::EcT<Fp2> G2;
	mpz_class z;
	mpz_class p;
	mpz_class r;
	Fp Z;
	Fp2 W2p;
	Fp2 W3p;
	static const size_t gammarN = 5;
	Fp2 gammar[gammarN];
	Fp2 gammar2[gammarN];
	Fp2 gammar3[gammarN];
	int b;
	Fp2 b_invxi; // b_invxi = b/xi of twist E' : Y^2 = X^3 + b/xi
	Fp half;

	// Loop parameter for the Miller loop part of opt. ate pairing.
	typedef std::vector<int8_t> SignVec;
	SignVec siTbl;
	bool useNAF;
	SignVec zReplTbl; // QQQ : snark

	void init(const CurveParam& cp = CurveFp254BNb, fp::Mode mode = fp::FP_AUTO)
	{
		z = cp.z;
		const int pCoff[] = { 1, 6, 24, 36, 36 };
		const int rCoff[] = { 1, 6, 18, 36, 36 };
		const int tCoff[] = { 1, 0,  6,  0,  0 };
		p = eval(pCoff, z);
		r = eval(rCoff, z);
		mpz_class t = eval(tCoff, z);
		Fp::init(p.get_str(), mode);
		Fp2::init(cp.xi_a);
		b = cp.b; // set b before calling Fp::init
		half = Fp(1) / Fp(2);
		Fp2 xi(cp.xi_a, 1);
		b_invxi = Fp2(b) / xi;
		G1::init(0, b, mcl::ec::Proj);
		G2::init(0, b_invxi, mcl::ec::Proj);
		power(gammar[0], xi, (p - 1) / 6);

		for (size_t i = 1; i < gammarN; i++) {
			gammar[i] = gammar[i - 1] * gammar[0];
		}

		for (size_t i = 0; i < gammarN; i++) {
			gammar2[i] = Fp2(gammar[i].a, -gammar[i].b) * gammar[i];
			gammar3[i] = gammar[i] * gammar2[i];
		}

		power(W2p, xi, (p - 1) / 3);
		power(W3p, xi, (p - 1) / 2);
		Fp2 tmp;
		Fp2::power(tmp, xi, (p * p - 1) / 6);
		assert(tmp.b.isZero());
		Fp::sqr(Z, tmp.a);

		const mpz_class largest_c = abs(6 * z + 2);
		useNAF = getGoodRepl(siTbl, largest_c);
		getGoodRepl(zReplTbl, abs(z)); // QQQ : snark
	}
	mpz_class eval(const int c[5], const mpz_class& x) const
	{
		return (((c[4] * x + c[3]) * x + c[2]) * x + c[1]) * x + c[0];
	}
};

template<class G>
void Frobenius(G& y, const G& x, const mpz_class& p)
{
	using namespace mcl;
	power(y.x, x.x, p);
	power(y.y, x.y, p);
	power(y.z, x.z, p);
}

template<class Fp>
struct BNT {
	typedef mcl::Fp2T<Fp> Fp2;
	typedef mcl::Fp6T<Fp> Fp6;
	typedef mcl::Fp12T<Fp> Fp12;
	typedef mcl::EcT<Fp> G1;
	typedef mcl::EcT<Fp2> G2;
	typedef ParamT<Fp> Param;
	static Param param;
	static void init(const mcl::bn::CurveParam& cp)
	{
		param.init(cp);
	}
	/*
		l = (a, b, c) => (a, b * P.y, c * P.x)
	*/
	static void updateLine(Fp6& l, const G1& P)
	{
		l.b.a *= P.y;
		l.b.b *= P.y;
		l.c.a *= P.x;
		l.c.b *= P.x;
	}
	static void mulB_invxi(Fp2& y, const Fp2& x)
	{
		Fp2::mul(y, x, param.b_invxi); // QQQ
	}
	static void dblLineWithoutP(Fp6& l, const G2& Q)
	{
		Fp2 A, B, C, D, E, F, X3, G, Y3, H, Z3, I, J;
		Fp2::mul(A, Q.x, Q.y);
		Fp2::divBy2(A, A);
		Fp2::sqr(B, Q.y);
		Fp2::sqr(C, Q.z);
		Fp2::add(D, C, C); D += C; // D = 3C
		mulB_invxi(E, D);
		Fp2::sqr(J, Q.x);
		Fp2::add(F, E, E); F += E; // F = 3E
		Fp2::add(H, Q.y, Q.z);
		Fp2::sqr(H, H);
		H -= B;
		H -= C;
		Fp2::sub(Q.x, B, F);
		Q.x *= A;
		Fp2::add(G, B, F);
		Fp2::divBy2(G, G);
		Fp2::sqr(Q.y, G); // G^2
		F *= E;// F = 3E^2
		Q.y -= F;
		Fp2::mul(Q.z, B, H);
		Fp2::sub(I, E, B);
		l.clear();
		l.a.a = I.a - I.b;
		l.a.b = I.a + I.b;
		l.b = -H;
		Fp2::add(l.c, J, J);
		l.c += J;
	}
	static void mulOpt1(Fp2& z, const Fp2& x, const Fp2& y)
	{
		Fp d0;
		Fp s, t;
		Fp::add(s, x.a, x.b);
		Fp::add(t, y.a, y.b);
		Fp::mul(d0, x.b, y.b);
		Fp::mul(z.a, x.a, y.a);
		Fp::mul(z.b, s, t);
		z.b -= z.a;
		z.b -= d0;
		z.a -= d0;
	}
	static void addLineWithoutP(Fp6& l, G2& R, const G2& Q)
	{
#if 1
		const Fp2& X1 = R.x;
		const Fp2& Y1 = R.y;
		const Fp2& Z1 = R.z;
		const Fp2& X2 = Q.x;
		const Fp2& Y2 = Q.y;
		Fp2 theta, lambda;
		theta = Y1 - Y2 * Z1;
		lambda = X1 - X2 * Z1;
		Fp2 lambda2;
		Fp2::sqr(lambda2, lambda);
		Fp2 t1, t2, t3, t4;
		t1 = X1 * lambda2;
		t2 = t1 + t1; // 2 X1 lambda^2
		t3 = lambda2 * lambda; // lambda^3
		Fp2::sqr(t4, theta);
		t4 *= Z1; // t4 = Z1 theta^2
		R.x = lambda * (t3 + t4 - t2);
		R.y = theta * (t2 + t1 - t3 - t4) - Y1 * t3;
		R.z = Z1 * t3;
		l.a = theta * X2 - lambda * Y2;
		Fp2::mulXi(l.a, l.a);
		l.b = lambda;
		l.c = -theta;
#else
		Fp2 t1, t2, t3, t4, T1, T2;
		Fp2::mul(t1, R.z, Q.x);
		Fp2::mul(t2, R.z, Q.y);
		Fp2::sub(t1, R.x, t1);
		Fp2::sub(t2, R.y, t2);
		Fp2::sqr(t3, t1);
		Fp2::mul(R.x, t3, R.x);
		Fp2::sqr(t4, t2);
		t3 *= t1;
		t4 *= R.z;
		t4 += t3;
		t4 -= R.x;
		t4 -= R.x;
		R.x -= t4;
		mulOpt1(T1, t2, R.x);
		mulOpt1(T2, t3, R.y);
		Fp2::sub(R.y, T1, T2);
		Fp2::mul(R.x, t1, t4);
		Fp2::mul(R.z, t3, R.z);
		Fp2::neg(l.c, t2);
		mulOpt1(T1, t2, Q.x);
		mulOpt1(T2, t1, Q.y);
		Fp2::sub(t2, T1, T2);
		Fp2::mulXi(l.a, t2);
		l.b = t1;
#endif
	}
	static void dblLine(Fp6& l, G2& Q, const G1& P)
	{
		dblLineWithoutP(l, Q);
		updateLine(l, P);
	}
	static void addLine(Fp6& l, G2& R, const G2& Q, const G1& P)
	{
		addLineWithoutP(l, R, Q);
		updateLine(l, P);
	}
	static void convertFp6toFp12(Fp12& y, const Fp6& x)
	{
		y.clear();
		y.a.a = x.a;
		y.a.c = x.c;
		y.b.b = x.b;
	}
	static void mul_024(Fp12&y, const Fp6& x)
	{
		Fp12 t;
		convertFp6toFp12(t, x);
		y *= t;
	}
	static void mul_024_024(Fp12& z, const Fp6& x, const Fp6& y)
	{
		Fp12 x2, y2;
		convertFp6toFp12(x2, x);
		convertFp6toFp12(y2, y);
		Fp12::mul(z, x2, y2);
	}
	static void optimalAtePairing(Fp12& f, const G2& Q, const G1& P)
	{
#if 1
		P.normalize();
		Q.normalize();
		const mpz_class& p = param.p;
		Fp6 l;
		G2 T = Q;
		f = 1;
		G2 negQ;
		G2::neg(negQ, Q);
		Fp6 d;
		dblLine(d, T, P);
		Fp6 e;
		assert(param.siTbl[1] == 1);
		addLine(e, T, Q, P);
		mul_024_024(f, d, e);
		for (size_t i = 2; i < param.siTbl.size(); i++) {
			dblLine(l, T, P);
			Fp12::sqr(f, f);
			mul_024(f, l);
			if (param.siTbl[i] > 0) {
				addLine(l, T, Q, P);
				mul_024(f, l);
			} else if (param.siTbl[i] < 0) {
				addLine(l, T, negQ, P);
				mul_024(f, l);
			}
		}
		G2 Q1, Q2;
		Frobenius(Q1, Q, p);
PUT(Q1);
		Frobenius(Q2, Q1, p);
		if (param.z < 0) {
			G2::neg(T, T);
			Fp12::inv(f, f);
		}
		addLine(l, T, Q1, P);
		mul_024(f, l);
		T += Q1;
		addLine(l, T, -Q2, P);
		mul_024(f, l);
		mpz_class a = p * p * p;
		a *= a;
		a *= a;
		a = (a - 1) / param.r;
		Fp12::power(f, f, a);
#else
		P.normalize();
		const mpz_class& p = param.p;
		const mpz_class s = abs(6 * param.z + 2);
		G2 T = Q;
		Fp6 l;
		f = 1;
		const int c = (int)mcl::gmp::getBitSize(s);
		for (int i = c - 2; i >= 0; i--) {
			Fp12::sqr(f, f);
			dblLine(l, T, P);
			mul(f, t);
			f *= t;
			G2::dbl(T, T);
			if (mcl::gmp::testBit(s, i)) {
				evalLine(t, T, Q, P);
				f *= t;
				T += Q;
			}
		}
		G2 Q1, Q2;
		Frobenius(Q1, Q, p);
		Frobenius(Q2, Q1, p);
		if (param.z < 0) {
			G2::neg(T, T);
			Fp12::inv(f, f);
		}
		evalLine(t, T, Q1, P);
		f *= t;
		T += Q1;
		evalLine(t, T, -Q2, P);
		f *= t;
		mpz_class a = p * p * p;
		a *= a;
		a *= a;
		a = (a - 1) / param.r;
		Fp12::power(f, f, a);
#endif
	}
};

template<class Fp>
ParamT<Fp> BNT<Fp>::param;

} } // mcl::bn

