#pragma once
#include "PriceAssessment.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace control {
inline constexpr double kRelativeChargeEfficiency=0.90;
inline bool relativeStorageProfitable(double future,double opportunity,float margin) {
    return float(future-opportunity/kRelativeChargeEfficiency)>=margin;
}
enum class PriceTier : uint8_t { Unknown, Cheap, Neutral, Expensive, VeryExpensive };
inline const char* priceTierName(PriceTier t) {
    switch(t){case PriceTier::Cheap:return "cheap";case PriceTier::Neutral:return "neutral";
    case PriceTier::Expensive:return "expensive";case PriceTier::VeryExpensive:return "very_expensive";default:return "unknown";}
}
inline bool validRelativeTiers(const PreparedSettings& c) {
    return std::isfinite(c.storageMinimumNetAdvantageEurKwh)&&c.storageMinimumNetAdvantageEurKwh>=0&&
        std::isfinite(c.cheapTimePct)&&std::isfinite(c.veryExpensiveTimePct)&&std::isfinite(c.additionalExpensiveTimePct)&&
        c.cheapTimePct>=0&&c.veryExpensiveTimePct>=0&&c.additionalExpensiveTimePct>=0&&
        double(c.cheapTimePct)+c.veryExpensiveTimePct+c.additionalExpensiveTimePct<=100&&
        validForecastPremiums(c)&&
        std::isfinite(c.chargePriorityThreshold)&&c.chargePriorityThreshold>0&&c.chargePriorityThreshold<100&&
        c.chargeInverterSharePct>=0&&c.chargeInverterSharePct<=100;
}
// Fixed BSS workspace. Whole equal-price blocks are never split by time order.
class RelativeTiers {
public:
    static constexpr unsigned kSlots=144;
    void set(unsigned i,double value){values_[i]=value;}
    bool classify(const PreparedSettings& c) {
        valid_=false;
        if(!validRelativeTiers(c))return false;
        for(unsigned i=0;i<kSlots;++i){if(!std::isfinite(values_[i]))return false;order_[i]=uint16_t(i);}
        std::sort(order_,order_+kSlots,[&](unsigned a,unsigned b){return values_[a]==values_[b]?a<b:values_[a]<values_[b];});
        if(value(0)==value(kSlots-1)){std::fill(tiers_,tiers_+kSlots,PriceTier::Neutral);valid_=true;return true;}
        const unsigned bottom=ceilBlock(1),top=floorBlock(kSlots-1);
        unsigned high=c.veryExpensiveTimePct>0?floorBlock(kSlots*(1-double(c.veryExpensiveTimePct)/100)):kSlots;
        if(c.cheapTimePct>0&&c.veryExpensiveTimePct>0)high=std::max(high,bottom);
        if(c.veryExpensiveTimePct>0)high=std::min(high,top);
        const unsigned cheap=c.cheapTimePct>0?std::min(high,ceilBlock(kSlots*double(c.cheapTimePct)/100)):0;
        const unsigned medium=c.additionalExpensiveTimePct>0?
            std::max(cheap,std::min(high,floorBlock(double(high)-kSlots*double(c.additionalExpensiveTimePct)/100))):high;
        for(unsigned rank=0;rank<kSlots;++rank)tiers_[order_[rank]]=rank<cheap?PriceTier::Cheap:
            (rank<medium?PriceTier::Neutral:(rank<high?PriceTier::Expensive:PriceTier::VeryExpensive));
        valid_=true;return true;
    }
    double assessed(unsigned i)const{return values_[i];}
    PriceTier tier(unsigned i)const{return valid_&&i<kSlots?tiers_[i]:PriceTier::Unknown;}
    unsigned count(PriceTier t)const{unsigned n=0;for(unsigned i=0;i<kSlots;++i)n+=tier(i)==t;return n;}
    double boundary(PriceTier t)const{
        double result=NAN;
        for(unsigned i=0;i<kSlots;++i)if(tier(i)==t)
            if(!std::isfinite(result)||(t==PriceTier::Cheap?values_[i]>result:values_[i]<result))result=values_[i];
        return result;
    }
private:
    double values_[kSlots]={};uint16_t order_[kSlots]={};PriceTier tiers_[kSlots]={};bool valid_=false;
    double value(unsigned rank)const{return values_[order_[rank]];}
    unsigned floorBlock(double target)const{unsigned result=0;for(unsigned i=1;i<=kSlots;++i)if((i==kSlots||value(i-1)!=value(i))&&i<=target)result=i;return result;}
    unsigned ceilBlock(double target)const{if(target<=0)return 0;for(unsigned i=1;i<=kSlots;++i)if((i==kSlots||value(i-1)!=value(i))&&i>=target)return i;return kSlots;}
};
}
