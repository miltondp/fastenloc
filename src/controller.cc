#include "controller.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string.h>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <gsl/gsl_rng.h>
#include <math.h>
#include <omp.h>


void controller::load_eqtl(char *eqtl_file, char *tissue){

    fprintf(stderr,"Processing eQTL annotations ... \n");
    ifstream dfile(eqtl_file, ios_base::in | ios_base::binary);
    boost::iostreams::filtering_istream in;

    in.push(boost::iostreams::gzip_decompressor());
    in.push(dfile);


    string line;
    istringstream ins;

    string target_tissue = string(tissue);


    string chr;
    string pos;
    string snp_id; // important
    string allele1;
    string allele2;
    string content;

    string delim = "|";

    while(getline(in,line)){

        ins.clear();
        ins.str(line);


        if(ins>>chr>>pos>>snp_id>>allele1>>allele2>>content){

            // passing content
            size_t pos = 0;
            string token;



            while ((pos = content.find(delim)) != string::npos) {
                token = content.substr(0, pos);
                size_t pos1 = token.find("@");
                size_t pos2 = token.find("=");
                size_t pos3 = token.find("[");
                size_t pos4 = token.find("]");
                string sig_id;
                string tissue_type;
                if(pos1 != string::npos){
                    tissue_type = token.substr(pos1+1,pos2-pos1-1);
                    sig_id = token.substr(0,pos1);
                }else{
                    sig_id = token.substr(0,pos2);
                }

                string pip = token.substr(pos2+1,pos3-pos2-1);
                string sig_pip = token.substr(pos3+1, pos4-pos3-1);
                //processing

                if(strlen(tissue)==0 || tissue_type == target_tissue){    


                    if(snp_index.find(snp_id) == snp_index.end()){
                        snp_index[snp_id] = snp_vec.size();
                        snp_vec.push_back(snp_id);
                    }


                    if(eqtl_sig_index.find(sig_id) == eqtl_sig_index.end()){
                        eqtl_sig_index[sig_id] = eqtl_sig_index.size();
                        sigCluster cluster;
                        cluster.id = sig_id;
                        cluster.cpip = 0;
                        eqtl_vec.push_back(cluster);
                    }

                    int index = eqtl_sig_index[sig_id];
                    double val = stod(pip);
                    eqtl_vec[index].cpip += val;
                    eqtl_vec[index].snp_vec.push_back(snp_id);
                    eqtl_vec[index].pip_vec.push_back(val);

                    //std::cout << snp_id<< "    "<< sig_id<<"  "<< tissue_type<<"   "<< pip << " "<< sig_pip<< std::endl;
                }


                content.erase(0, pos + delim.length());
            }
        }

    }
    double sum = 0;
    for(int i=0;i<eqtl_vec.size();i++){
        sum += eqtl_vec[i].cpip;
    }

    fprintf(stderr, "read in %d SNPs, %d eQTL signal clusters, %.1f expected eQTLs\n\n", int(snp_vec.size()), int(eqtl_vec.size()), sum);


}


void controller::load_gwas_torus(char *gwas_file){

    fprintf(stderr,"Processing complex trait data ... \n");
    ifstream dfile(gwas_file, ios_base::in | ios_base::binary);
    boost::iostreams::filtering_istream in;

    in.push(boost::iostreams::gzip_decompressor());
    in.push(dfile);


    string line;
    istringstream ins;


    string snp_id; // important
    string sig_id;
    double prior;
    double posterior;

    string delim = "|";

    double gwas_sum = 0;
    int gwas_count = 0;
    while(getline(in,line)){

        ins.clear();
        ins.str(line);


        if(ins>>snp_id>>sig_id>>posterior>>prior){
            if(snp_index.find(snp_id) == snp_index.end()){
                snp_index[snp_id] = snp_vec.size();
                snp_vec.push_back(snp_id);
            }


            if(gwas_sig_index.find(sig_id) == gwas_sig_index.end()){
                gwas_sig_index[sig_id] = gwas_sig_index.size();
                sigCluster cluster;
                cluster.id = sig_id;
                cluster.cpip = 0;
                gwas_vec.push_back(cluster);
            }

            int index = gwas_sig_index[sig_id];
            gwas_vec[index].cpip += posterior;
            gwas_vec[index].snp_vec.push_back(snp_id);
            gwas_vec[index].pip_vec.push_back(posterior);
            gwas_sum += posterior;
            gwas_count++;
        }

    }

    gwas_pip_vec = vector<double>(snp_vec.size(),0.0);

    for(int i=0; i<gwas_vec.size();i++){
        for(int j=0;j<gwas_vec[i].snp_vec.size();j++){
            string snp = gwas_vec[i].snp_vec[j];
            gwas_pip_vec[snp_index[snp]]=gwas_vec[i].pip_vec[j];
        }
    }
    
    pi1 = gwas_sum/gwas_count; 

    fprintf(stderr, "read in %d SNPs (eQTL+gwas), %d GWAS loci, %.1f expected hits\n\n", int(snp_vec.size()), int(gwas_vec.size()), gwas_sum);
}



void controller::enrich_est(){

    const gsl_rng_type * T;
    gsl_rng * r;
    gsl_rng_env_setup();
    T = gsl_rng_default;
    r = gsl_rng_alloc (T);

    vector<double> a0_vec = vector<double>(ImpN, 0.0);
    vector<double> v0_vec = vector<double>(ImpN, 0.0);
    vector<double> a1_vec = vector<double>(ImpN, 0.0);
    vector<double> v1_vec = vector<double>(ImpN, 0.0);

    #pragma omp parallel for num_threads(nthread)
    for(int k=0; k< ImpN; k++){
   
        vector<int> eqtl_sample = vector<int>(snp_vec.size(),0);

        for(int i=0; i<eqtl_vec.size();i++){
            int rst = eqtl_vec[i].impute_qtn(r);
            if(rst >=0){
                string snp = eqtl_vec[i].snp_vec[rst];
                eqtl_sample[snp_index[snp]]=1;
            }
        }

        vector<double> rst = run_EM(eqtl_sample);
        #pragma omp critical
        {
            a0_vec[k] = rst[0];
            a1_vec[k] = rst[1];
            v0_vec[k] = rst[2];
            v1_vec[k] = rst[3];

        }
        fprintf(stderr, "Imputation round %2d is completed\n", k+1);
    }

   a0_est = 0;
   a1_est = 0;

    double var0 = 0;
    double var1 = 0;
    for(int k=0;k<ImpN;k++){
        a0_est += a0_vec[k];
        a1_est += a1_vec[k];
        var0 += v0_vec[k];
        var1 += v1_vec[k];
    }

    a0_est = a0_est/ImpN;
    a1_est = a1_est/ImpN;

    double bv0 = 0;
    double bv1 = 0;
    for(int k=0;k<ImpN;k++){

        bv0 += pow(a0_vec[k]-a0_est,2);
        bv1 += pow(a1_vec[k]-a1_est,2);

    }

    bv0 = bv0/(ImpN-1);
    bv1 = bv1/(ImpN-1);
    var0 = var0/ImpN;
    var1 = var1/ImpN;


    double sd0 = sqrt(var0 + bv0*(ImpN+1)/ImpN);
    double sd1 = sqrt(var1 + bv1*(ImpN+1)/ImpN);
    string  enrich_file = prefix + string("enloc.enrich.out");
    FILE *fd = fopen(enrich_file.c_str(), "w");
    
    fprintf(fd, "%10s   %7.3f     %7.3f\n","Intercept", a0_est, sd0);
    fprintf(fd, "%10s   %7.3f     %7.3f\n","Enrichment", a1_est, sd1);

    fclose(fd);

    pi1_e = exp(a0_est+a1_est)/(1+exp(a0_est + a1_est));
    pi1_ne =  exp(a0_est)/(1+exp(a0_est));

    //printf("%7.3e  %7.3e     %7.3e\n", pi1_e, pi1_ne, pi1);

    fprintf(stderr, "\nEnrichment analysis is completed\n");

    gsl_rng_free(r);
    return;

}




void controller::compute_coloc_prob(){

    fprintf(stderr, "\nComputing colocalization probabilities ... \n\n");
 
    string snp_file = prefix + string("enloc.snp.out");
    string sig_file = prefix + string("enloc.sig.out");
    FILE * fd1 = fopen(snp_file.c_str(),"w");
    FILE * fd2 = fopen(sig_file.c_str(),"w");
    
    double r_null = pi1/(1-pi1);
    double r1 = pi1_e/(1-pi1_e);
    double r0 = pi1_ne/(1-pi1_ne);


    for(int i=0;i<eqtl_vec.size();i++){
        eqtl_vec[i].coloc_prob = 0;    
        vector<double> gprob_vec_null;
        double gprob_null = 0;
        vector<double> gbf_vec;

        for(int k=0;k<eqtl_vec[i].snp_vec.size();k++){
            string snp = eqtl_vec[i].snp_vec[k];
            double d = gwas_pip_vec[snp_index[snp]];
            gprob_vec_null.push_back(d);
            gprob_null += d;
        }

        if(gprob_null > 1-1e-5){
            // renormalize
            for(int k=0;k<eqtl_vec[i].snp_vec.size();k++){
                gprob_vec_null[k] = (1-1e-5)*(gprob_vec_null[k]/gprob_null);
            }

            gprob_null = 1-1e-5;
        }
        
        double nc = ((1-pi1)/pi1)/(1-gprob_null);
        double sum_bf = nc - (1-pi1)/pi1;
        for(int k=0;k<eqtl_vec[i].snp_vec.size();k++){
            string snp = eqtl_vec[i].snp_vec[k];
            double d = gprob_vec_null[k];
            double r = eqtl_vec[i].pip_vec[k];
            double bf = d*nc;
            double prob = pi1_e*(1-pi1_ne)*bf/((1-pi1_ne)*(1-pi1_e)+pi1_ne*(1-pi1_e)*(sum_bf-bf)+pi1_e*(1-pi1_ne)*bf);
            // snp level coloc prob
            double p_coloc = prob*r;
            double gprob_e = p_coloc;
            eqtl_vec[i].coloc_prob += p_coloc;

            //other non-coloc possibilities
            /*
           prob = bf/((1-pi1_ne)/pi1_ne + sum_bf)   
           gprob_e += prob*(1-eqtl_vec[i].cpip);
           for(int j=0;j<eqtl_vec[i].snp_vec.size();j++){
               if(j==k)
                   continue;
               prob = pi1_ne*(1-pi1_e)*bf/( (1-pi1_e)*(1-pi1_ne) + pi1_ne*(1-pi1_e)*(sum_bf-bf[j])+pi1_e*(1-pi1_ne)*bf[j]);
               gprob_e += prob* eqtl_vec[i].pip_vec[j];
           }
           */ 
           fprintf(fd1,"%15s   %15s   %7.3e %7.3e         %7.3e\n",eqtl_vec[i].id.c_str(), snp.c_str(), r,d,  p_coloc);
        }
            

        fprintf(fd2, "%15s   %4d  %7.3e %7.3e   %7.3e\n",eqtl_vec[i].id.c_str(), int(eqtl_vec[i].snp_vec.size()), eqtl_vec[i].cpip, gprob_null, eqtl_vec[i].coloc_prob);

    }

    fclose(fd1);
    fclose(fd2);
}


vector<double> controller::run_EM(vector<int> &eqtl_sample){


    double a0 = log(pi1/(1-pi1));
    double a1 = 0;
    double var0;
    double var1;
    double r1 = exp(a0+a1);
    double r0 = exp(a0);
    double r_null = pi1/(1-pi1);




    while(1){

        // E-step
        double e0g0 = 0;
        double e0g1 = 0;
        double e1g0 = 0;
        double e1g1 = 0;

        for(int i=0;i<snp_vec.size();i++){

            double val = gwas_pip_vec[i];
            if(val==1)
                val = 1 - 1e-8;
            val = val/(1-val);   
            if(eqtl_sample[i]==0){
                val = r0*val/r_null;
                val = val/(1+val);
                e0g1 += val;
                e0g0 += 1 - val;
            }   

            if(eqtl_sample[i]==1){
                val = r1*val/r_null;
                val = val/(1+val);
                e1g1 += val;
                e1g0 += 1 - val;
            }

        }

        // M-step

        double a1_new = log(e1g1*e0g0/(e1g0*e0g1));
        if(fabs(a1_new-a1)<0.01){
            a1 = a1_new;
            var1 = 1.0/e0g0 + 1.0/e1g0 + 1.0/e1g1 + 1.0/e0g1;
            var0 = sqrt(1.0/e0g1 + 1.0/e0g0);
            break;
        }

        a1 = a1_new;
        a0 = log(e0g1/e0g0);

        r0 = exp(a0);
        r1 = exp(a0+a1);


    }
    vector<double> av;
    av.push_back(a0);
    av.push_back(a1);
    av.push_back(var0);
    av.push_back(var1);
    return(av);
}

void controller::set_prefix(char *str){

    if(strlen(str) == 0){
        prefix = string("");
    }else{
        prefix = string(str)+string(".");
    }
}



