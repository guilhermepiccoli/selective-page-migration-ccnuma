/* *********************************************************************
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * AND the GNU Lesser General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors of this code:
 *   Henrique Nazaré Santos  <hnsantos@gmx.com>
 *   Guilherme G. Piccoli    <porcusbr@gmail.com>
 *
 * Publication:
 *   Compiler support for selective page migration in NUMA
 *   architectures. PACT 2014: 369-380.
 *   <http://dx.doi.org/10.1145/2628071.2628077>
********************************************************************* */
#ifndef _EXPR_H_
#define _EXPR_H_

#include "llvm/IR/IRBuilder.h"

#include "ginac/ginac.h"

#include <string>
#include <unordered_set>

using namespace llvm;

using std::map;
using std::set;
using std::pair;
using std::string;
using std::vector;


class Expr;

// Wrapper arround GiNaC::exmap. Used for expression matching.
class ExprMap {
public:
	Expr operator[](const Expr& Key);

	size_t size() const;
	GiNaC::exmap& getMap();

private:
	GiNaC::exmap Map_;
};


class Expr {
public:
	// Initilizes to zero.
	Expr();
	Expr(long Int);
	Expr(long Numer, long Denom);
	Expr(double Float);
	Expr(APInt Int);
	Expr(GiNaC::ex Ex);
	Expr(Twine Name);
	Expr(string Name);
	Expr(Value *V);
	Expr(Value *V, Value *R); //constructor that replaces an Expr Value* from R to V

	template<class T>
  
	struct __iterator {
		T It;
		
		Expr operator*() const { return Expr(*It); }

		__iterator<T>& operator++() {
			++It;
			return *this;
		}

		__iterator<T> operator++(int) {
			struct __iterator Copy = { It };
			++It;
			return Copy;
		}

		bool operator==(__iterator<T>& Other) const { return It == Other.It; }

		bool operator!=(__iterator<T>& Other) const { return It != Other.It; }
	};

	typedef __iterator<GiNaC::const_iterator> iterator;
	typedef __iterator<GiNaC::const_preorder_iterator> preorder_iterator;

	iterator begin() {  return { Expr_.begin() };  }
	
	iterator end() {  return { Expr_.end() };  }

	preorder_iterator preorder_begin() const {  return { Expr_.preorder_begin() };  }
	
	preorder_iterator preorder_end() const {  return { Expr_.preorder_end() };  }


	Expr at(unsigned Idx) const;
	size_t nops()         const;

	bool isValid()    const;
	bool isSymbol()   const;
	bool isAdd()      const;
	bool isMul()      const;
	bool isPow()      const;
	bool isMin()      const;
	bool isMax()      const;
	bool isConstant() const; // Constants include any floating-point number, integer or rational.
	bool isInteger()  const;
	bool isRational() const;
	bool isFloat()    const;
	bool isPositive() const;
	bool isNegative() const;

	long   getInteger()       const;
	double getFloat()         const;
	long   getRationalNumer() const;
	long   getRationalDenom() const;

	string getSymbolString() const;

	vector<Expr> getSymbols() const;

	Expr getPowBase() const;
	Expr getPowExp()  const;

	Value *getSymbolValue() const;

	Value *getValue(IntegerType *Ty, IRBuilder<> &IRB) const;
	Value *getValue(unsigned BitWidth, LLVMContext &C, IRBuilder<> &IRB) const;

	Value *getExprValue(IntegerType *Ty, IRBuilder<> &IRB, Module *M, Expr *Parent = nullptr)   const; // *Parent is currently not used
	Value *getExprValue(unsigned BitWidth, IRBuilder<> &IRB, Module *M, Expr *Parent = nullptr) const; // *Parent is currently not used

	bool eq        (const Expr& Other) const;
	bool ne        (const Expr& Other) const;
	bool operator==(const Expr& Other) const;
	bool operator!=(const Expr& Other) const;

	Expr operator+(const Expr& Other)  const;
	Expr operator+(unsigned Other)     const;
	Expr operator-(const Expr& Other)  const;
	Expr operator-(unsigned Other)     const;
	Expr operator*(const Expr& Other)  const;
	Expr operator*(unsigned Other)     const;
	Expr operator/(const Expr& Other)  const;
	Expr operator/(unsigned Other)     const;
	Expr operator^(const Expr& Other)  const;
	Expr operator^(unsigned Other)     const;

	Expr min(Expr Other) const;
	Expr max(Expr Other) const;

	Expr subs(Expr This, Expr That)   const;
	bool match(Expr Ex, ExprMap& Map) const;
	bool match(Expr Ex)               const;
	bool has(Expr Ex)                 const;

	static Expr InvalidExpr();
	static Expr WildExpr();

	friend raw_ostream& operator<<(raw_ostream& OS, const Expr& EI);
	friend class ExprMap;
	
protected:
	GiNaC::ex getExpr() const;

private:
	GiNaC::ex Expr_;
};

#endif
