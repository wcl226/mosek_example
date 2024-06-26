#include <iostream>
#include "fusion.h"

#include <random>

using namespace mosek::fusion;
using namespace monty;

// A helper function which returns a slice corresponding to j-th variable
Variable::t slice(Variable::t X, int d, int j) 
{
    return X->slice(new_array_ptr<int,1>({j,0,0}), new_array_ptr<int,1>({j+1,d,d}))
            ->reshape(new_array_ptr<int,1>({d,d}));
}

int main(int argc, char *argv[])
{
    //todo https://docs.mosek.com/latest/cxxfusion/tutorial-sdo-shared.html#
    std::random_device rd; // 随机数种子
    std::mt19937 e2(rd()); //  创建了一个 Mersenne Twister 伪随机数生成器对象
    std::uniform_real_distribution<> dist(0, 1); // 决定了范围

    // Sample data 
    int n = 100, d = 4, k = 3;
    std::vector<double> b({9, 10, 11});
    std::vector<std::shared_ptr<ndarray<double,2>> > A;
    for(int i=0; i<k*n; i++) 
    {
        auto Ai = std::make_shared<ndarray<double,2>>(shape(d,d));
        for(int s1=0; s1<d; s1++)
            for(int s2=0; s2<=s1; s2++)
                (*Ai)(s1,s2) = (*Ai)(s2,s1) = dist(e2); // 使用 高质量的伪随机数生成器 符合均匀分布的随机实数值
        A.push_back(Ai);
    }

    //* Create a model with n semidefinite variables od dimension d x d
    Model::t M = new Model("sdo3"); auto _M = finally([&]() { M->dispose(); });

    Variable::t X = M->variable(Domain::inPSDCone(d, n)); // (n, d, d): n个维度的d*d维的对称半定矩阵

    // Pick indexes (j, s, s), j=0..n-1, s=0..d, of diagonal entries for the objective
    auto alldiag =  std::make_shared<ndarray<int,2>>(
            shape(d*n,3), // 每一个X_j 对角线上元素存在d个, 一共有n个X_j 
            std::function<int(const shape_t<2> &)>([d](const shape_t<2> & p) { return p[1]==0 ? p[0]/d : p[0]%d; }));

    M->objective(ObjectiveSense::Minimize, Expr::sum( X->pick(alldiag) ));

    // Each constraint is a sum of inner products
    // Each semidefinite variable is a slice of X
    for(int i=0; i<k; i++) 
    {
        std::vector<Expression::t> sumlist; // 创建一个表达式的 list
        for(int j=0; j<n ;j++)
            sumlist.push_back( Expr::dot(A[i*n+j], slice(X, d, j)) ); // 逐表达式：AX_j

        M->constraint(Expr::add(new_array_ptr(sumlist)), Domain::greaterThan(b[i])); // 有k个X_j
    }

    // Solve
    M->setLogHandler([ = ](const std::string & msg) { std::cout << msg << std::flush; } );            // Add logging
    M->writeTask("sdo3.ptf");                // Save problem in readable format
    M->solve();

    // Get results. Each variable is a slice of X
    std::cout << "Contributing variables:" << std::endl;
    for(int j=0; j<n; j++)  // n个X
    {
        auto Xj = *(slice(X, d, j)->level()); // 对第j个X
        double maxval = 0;
        for(int s=0; s<d*d; s++) 
            maxval = std::max(maxval, Xj[s]); // const double* 类似一维存储
        if (maxval>1e-6) 
        {
            std::cout << "X" << j << "=" << std::endl;
            for(int s1=0; s1<d; s1++) 
            {
                for(int s2=0; s2<d; s2++) std::cout << Xj[s1*d+s2] << "  ";
                std::cout << std::endl;
            }
        }
    }


    return 0;
}
