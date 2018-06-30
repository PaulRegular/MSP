#include <TMB.hpp>

// Function for keeping values positive
template<class Type>
Type pos_fun(Type x, Type eps, Type &pen){
    pen += CppAD::CondExpLt(x, eps, Type(0.01) * pow(x - eps, 2), Type(0));
    return CppAD::CondExpGe(x, eps, x, eps / (Type(2) - x / eps));
}

template<class Type>
Type objective_function<Type>::operator() ()
{

    // Data inputs
    DATA_VECTOR(L);
    DATA_IVECTOR(L_species);
    DATA_IVECTOR(L_year);
    DATA_VECTOR(I);
    DATA_IVECTOR(I_species);
    DATA_IVECTOR(I_survey);  // note: survey factor is species specific
    DATA_IVECTOR(I_sy);      // species-year index that corresponds to L row number
    DATA_SCALAR(min_P);
    DATA_IMATRIX(cor_ind);   // indexing for the correlation matrix
    DATA_INTEGER(nY);        // number of years

    // Parameters
    PARAMETER_VECTOR(log_P);
    PARAMETER_VECTOR(log_sd_P);
    PARAMETER_VECTOR(logit_cor);
    PARAMETER_VECTOR(log_K);
    PARAMETER_VECTOR(log_r);
    PARAMETER_VECTOR(log_m);
    PARAMETER_VECTOR(log_q);
    PARAMETER_VECTOR(log_sd_I);

    // Transformations
    vector<Type> log_I = log(I);
    vector<Type> P = exp(log_P);
    vector<Type> sd_P = exp(log_sd_P);
    vector<Type> cor = 2.0 / (1.0 + exp(-logit_cor)) - 1.0; // want cor to be between -1 and 1
    vector<Type> K = exp(log_K);
    vector<Type> r = exp(log_r);
    vector<Type> m = exp(log_m);
    vector<Type> sd_I = exp(log_sd_I);

    // Containers
    int nC = cor.size();          // number of correlation parameters
    int nS = sd_P.size();         // number of species
    matrix<Type> cor_mat(nS, nS);
    cor_mat.setZero();
    matrix<Type> sd_mat(nS, nS);
    sd_mat.setZero();
    matrix<Type> cov_mat(nS, nS);
    cov_mat.setZero();
    matrix<Type> epislon_P(nY, nS);
    int nL = L.size();
    vector<Type> pred_P(nL);
    vector<Type> log_pred_P(nL);
    vector<Type> log_res_P(nL);
    vector<Type> B(nL);
    vector<Type> log_B(nL);
    int nI = I.size();
    vector<Type> log_pred_I(nI);
    vector<Type> log_res_I(nI);
    vector<Type> std_res_I(nI);

    // Initalize nll
    Type pen = Type(0);
    Type nll = Type(0);
    Type dnorm_nll = Type(0);
    Type dmvnorm_nll = Type(0);

    // Set-up covariance matrix
    for (int i = 0; i < nS; i++) {
        for (int j = 0; j < nS; j++) {
            if (j == i) {
                cor_mat(i, j) = Type(1);
            }
        }
    }
    for (int i = 0; i < nC; i++) {
        cor_mat(cor_ind(i, 0), cor_ind(i, 1)) = cor(i);
        cor_mat(cor_ind(i, 1), cor_ind(i, 0)) = cor(i);
    }
    for (int i = 0; i < nS; i++) {
        sd_mat(i, i) = sd_P(i);
    }
    cov_mat = sd_mat * cor_mat * sd_mat;

    using namespace density;
    MVNORM_t<Type> dmvnorm(cov_mat);

    // Process equation
    for (int i = 0; i < nL; i++){
        if (L_year(i) == 0) {
            epislon_P(L_year(i), L_species(i)) = log_P(i) - Type(0);
            // nll -= dnorm(log_P(i), Type(0), sd_P(L_species(i)), true);
            dnorm_nll -= dnorm(log_P(i), Type(0), sd_P(L_species(i)), true);
            SIMULATE {
                log_P(i) = rnorm(Type(0), sd_P(L_species(i)));
            }
        } else {
            pred_P(i) = P(i - 1) +
                (r(L_species(i)) / (m(L_species(i)) - 1)) *
                (Type(1) - pow(P(i - 1), m(L_species(i)) - Type(1))) -
                (L(i - 1) / K(L_species(i)));
            pred_P(i) = pos_fun(pred_P(i), min_P, pen);
            epislon_P(L_year(i), L_species(i)) = log_P(i) - log(pred_P(i));
            // nll -= dnorm(log_P(i), log(pred_P(i)), sd_P(L_species(i)), true);
            dnorm_nll -= dnorm(log_P(i), log(pred_P(i)), sd_P(L_species(i)), true);
            SIMULATE {
                log_P(i) = rnorm(log(pred_P(i)), sd_P(L_species(i)));
            }
        }
        B(i) = P(i) * K(L_species(i));
    }

    for (int i = 0; i < nY; i++) {
        nll += dmvnorm(epislon_P.row(i));
        dmvnorm_nll += dmvnorm(epislon_P.row(i));
    }

    // Observation equations
    for (int i = 0; i < nI; i++){
        log_pred_I(i) = log_q(I_survey(i)) + log_P(I_sy(i)) + log_K(I_species(i));
        nll -= dnorm(log_I(i), log_pred_I(i), sd_I(I_survey(i)), true);
        log_res_I(i) = log_I(i) - log_pred_I(i);
        std_res_I(i) = log_res_I(i) / sd_I(I_survey(i));
        SIMULATE {
            log_I(i) = rnorm(log_pred_I(i), sd_I(I_survey(i)));
        }
    }

    // More transformations
    log_B = log(B);
    log_pred_P = log(pred_P);
    log_res_P = log_P - log_pred_P;

    // AD report values
    ADREPORT(log_P);
    ADREPORT(log_pred_P);
    ADREPORT(log_res_P);
    ADREPORT(log_B);
    ADREPORT(log_pred_I);
    ADREPORT(log_res_I);
    ADREPORT(std_res_I);

    // Report simulated values
    SIMULATE {
        REPORT(log_P);
        REPORT(log_I);
    }

    REPORT(cor_mat);
    REPORT(sd_mat);
    REPORT(cov_mat);
    REPORT(dnorm_nll);
    REPORT(dmvnorm_nll);
    REPORT(pen);
    nll += pen;
    return nll;

}

