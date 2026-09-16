// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
#include "EnhancerDSP.h"
#include <cstdio>
#include <cstring>
#include <string>
static std::vector<short> load(const std::string& fn){ FILE*f=fopen(fn.c_str(),"rb"); std::vector<short> v; if(!f){printf("missing %s\n",fn.c_str());return v;} fseek(f,0,SEEK_END); long n=ftell(f)/2; fseek(f,0,SEEK_SET); v.resize(n); fread(v.data(),2,n,f); fclose(f); return v; }
int main(){
    struct Cfg{const char*name; EnhancerDSP::Params p;};
    auto base=[](){ EnhancerDSP::Params p; p.volume=50; p.harmBass=0;p.harmBassRange=0;p.drumBass=0;p.drumBassRange=0;p.dry=100;p.harmTreble=0;p.harmTrebleRange=0;p.ambience=0;p.ambienceRange=0;p.boost=false; return p; };
    Cfg cfgs[7]; const char* names[7]={"off","hb50","hb100","db50","tr50","amb50","boost"};
    for(int i=0;i<7;i++){cfgs[i].name=names[i];cfgs[i].p=base();}
    cfgs[1].p.harmBass=50;cfgs[1].p.harmBassRange=50; cfgs[2].p.harmBass=100;cfgs[2].p.harmBassRange=100;
    cfgs[3].p.drumBass=50;cfgs[3].p.drumBassRange=50; cfgs[4].p.harmTreble=50;cfgs[4].p.harmTrebleRange=50;
    cfgs[5].p.ambience=50;cfgs[5].p.ambienceRange=50; cfgs[6].p.boost=true;
    const char* sigs[5]={"s40","s1k","mix","imp","burst"};
    for(auto&c:cfgs) for(auto s:sigs){
        auto in=load(std::string("../host/in_")+s+".raw"); auto ref=load(std::string("../host/out/")+c.name+"_"+s+".raw");
        if(in.empty()||ref.empty()) continue;
        EnhancerDSP d; d.prepare(44100,1); d.setParams(c.p);
        std::vector<float> x(in.size()), y(in.size());
        for(size_t i=0;i<in.size();i++) x[i]=in[i]/32768.0f;
        for(size_t off=0;off<x.size();off+=576){ int n=(int)std::min<size_t>(576,x.size()-off); d.process(&x[off],&y[off],n); }
        int maxd=0; long n2=0; for(size_t i=0;i<y.size();i++){ int v=(int)std::lround(y[i]*32768.0f); int dd=abs(v-ref[i]); if(dd>maxd)maxd=dd; if(dd>2)n2++; }
        printf("%-6s %-6s maxdiff=%5d  >2LSB=%ld\n",c.name,s,maxd,n2);
    }
    return 0;
}
