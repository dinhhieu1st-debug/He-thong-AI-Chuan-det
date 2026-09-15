#include "vitals_ai.h"

#include <math.h>
#include <string.h>

#define WINDOW 20U
#define BASELINE_SAMPLES 20U

static float hr_window[WINDOW], spo2_window[WINDOW];
static uint8_t window_count, window_head;
static float baseline_values[BASELINE_SAMPLES];
static uint8_t baseline_count;
static uint8_t votes[3], vote_count, vote_head;
static bool recalibrating, recalibration_done, test_mode;
static float candidate[BASELINE_SAMPLES];
static uint8_t candidate_count;

static void ordered(float *hr, float *oxygen) {
  for (uint8_t i=0;i<window_count;i++) {
    uint8_t index=window_count<WINDOW?i:(uint8_t)((window_head+i)%WINDOW);
    hr[i]=hr_window[index];oxygen[i]=spo2_window[index];
  }
}
static float mean(const float *v,uint8_t n){float x=0;for(uint8_t i=0;i<n;i++)x+=v[i];return x/n;}
static float stddev(const float *v,uint8_t n,float m){float x=0;for(uint8_t i=0;i<n;i++){float d=v[i]-m;x+=d*d;}return sqrtf(x/n);}
static float slope(const float *v,uint8_t n){float xm=(n-1U)/2.0f,ym=mean(v,n),top=0,bottom=0;for(uint8_t i=0;i<n;i++){float x=i-xm;top+=x*(v[i]-ym);bottom+=x*x;}return bottom?top/bottom:0;}
static float median20(const float *v,uint8_t n){float a[BASELINE_SAMPLES];for(uint8_t i=0;i<n;i++)a[i]=v[i];for(uint8_t i=1;i<n;i++){float x=a[i];int j=i-1;while(j>=0&&a[j]>x){a[j+1]=a[j];j--;}a[j+1]=x;}return n&1U?a[n/2U]:(a[n/2U-1U]+a[n/2U])/2.0f;}

/* alert_tree.json, exported losslessly as a compact node table (87f3833e). */
typedef struct { int8_t feature; float threshold; int8_t left, right, prediction; } node_t;
static const node_t nodes[] = {
 {0,106.3725f,1,60,1},{7,95.245f,2,11,1},{7,92.6775f,3,8,2},{9,91.25f,4,7,3},
 {7,89.7275f,5,6,3},{-1,0,-1,-1,3},{-1,0,-1,-1,3},{-1,0,-1,-1,2},
 {10,91.25f,9,10,2},{-1,0,-1,-1,3},{-1,0,-1,-1,2},{4,61.55f,12,21,1},
 {2,40.3f,13,14,2},{-1,0,-1,-1,3},{0,62.775f,15,18,2},{0,50.655f,16,17,2},
 {-1,0,-1,-1,2},{-1,0,-1,-1,2},{5,-.41361f,19,20,2},{-1,0,-1,-1,2},
 {-1,0,-1,-1,1},{6,11.8384f,22,45,1},{1,.710765f,23,34,1},{11,-.05312f,24,29,1},
 {13,-1.95f,25,26,2},{-1,0,-1,-1,3},{0,77.5875f,27,28,2},{-1,0,-1,-1,2},
 {-1,0,-1,-1,2},{11,-.01402f,30,33,1},{8,.152275f,31,32,1},{-1,0,-1,-1,1},
 {-1,0,-1,-1,1},{-1,0,-1,-1,1},{1,1.966025f,35,40,1},{8,.111525f,36,37,1},
 {-1,0,-1,-1,1},{1,.819985f,38,39,1},{-1,0,-1,-1,1},{-1,0,-1,-1,1},
 {1,2.88154f,41,44,1},{0,88.85f,42,43,1},{-1,0,-1,-1,1},{-1,0,-1,-1,1},
 {-1,0,-1,-1,2},{12,13.25f,46,53,2},{12,10.45f,47,52,2},{1,2.884035f,48,49,2},
 {-1,0,-1,-1,2},{0,97.7875f,50,51,2},{-1,0,-1,-1,2},{-1,0,-1,-1,2},
 {-1,0,-1,-1,2},{3,100.05f,54,55,3},{-1,0,-1,-1,3},{4,125.85f,56,59,3},
 {12,17.75f,57,58,3},{-1,0,-1,-1,3},{-1,0,-1,-1,3},{-1,0,-1,-1,3},
 {6,57.468325f,61,68,3},{0,141.455f,62,67,2},{0,127.5375f,63,66,2},{1,7.639245f,64,65,2},
 {-1,0,-1,-1,2},{-1,0,-1,-1,2},{-1,0,-1,-1,2},{-1,0,-1,-1,3},
 {0,134.18f,69,72,3},{3,141.75f,70,71,3},{-1,0,-1,-1,2},{-1,0,-1,-1,3},
 {-1,0,-1,-1,3}
};
static uint8_t tree(const float f[14]) {
  int8_t index=0;
  while(nodes[index].feature>=0)index=f[nodes[index].feature]<=nodes[index].threshold?nodes[index].left:nodes[index].right;
  return (uint8_t)nodes[index].prediction;
}

static uint8_t conservative(uint8_t predicted,const float f[14],float baseline) {
  float projected_hr=f[4]+fmaxf(-12.0f,fminf(12.0f,5.0f*f[5]));
  float projected_spo2=f[10]+fmaxf(-2.5f,fminf(2.5f,5.0f*f[11]));
  bool warning=(projected_hr<=35&&f[4]<=48)||(projected_hr>=140&&f[4]>=125)||(projected_spo2<=90&&f[10]<=93);
  float tolerance=fmaxf(15.0f,baseline*.20f);
  bool attention=(projected_hr<=50&&f[4]<=58)||(projected_hr>=101&&f[4]>=95)
                 ||(projected_spo2<=94&&f[10]<=96)
                 ||(fabsf(projected_hr-baseline)>tolerance&&fabsf(f[4]-baseline)>tolerance*.7f);
  if(predicted==3U&&!warning)return attention?2U:1U;
  if(predicted==2U&&!attention)return 1U;
  return predicted;
}

static uint8_t majority_vote(uint8_t next) {
  votes[vote_head]=next;vote_head=(uint8_t)((vote_head+1U)%3U);if(vote_count<3U)vote_count++;
  uint8_t counts[4]={0};for(uint8_t i=0;i<vote_count;i++)counts[votes[i]]++;
  uint8_t best=next;for(uint8_t level=1;level<=3;level++)if(counts[level]>counts[best])best=level;
  return best;
}

bool vitals_ai_init(void){vitals_ai_reset();return true;}
void vitals_ai_reset(void){memset(hr_window,0,sizeof(hr_window));memset(spo2_window,0,sizeof(spo2_window));memset(baseline_values,0,sizeof(baseline_values));window_count=window_head=baseline_count=vote_count=vote_head=0;recalibrating=recalibration_done=test_mode=false;candidate_count=0;}
void vitals_ai_begin_baseline_recalibration(void){recalibrating=true;recalibration_done=false;candidate_count=0;}
bool vitals_ai_baseline_recalibrating(void){return recalibrating;}
uint8_t vitals_ai_baseline_recalibration_samples(void){return candidate_count;}
bool vitals_ai_take_baseline_recalibration_completed(void){bool done=recalibration_done;recalibration_done=false;return done;}
void vitals_ai_clear_history(vitals_ai_result_t *r){window_count=window_head=vote_count=vote_head=0;if(r){r->history_ready=false;r->history_samples=0;r->level=1;r->ai_anomaly=false;}}
void vitals_ai_begin_test(void){test_mode=true;}
void vitals_ai_end_test(vitals_ai_result_t *r){test_mode=false;vote_count=vote_head=0;if(r){r->level=1;r->ai_anomaly=false;}}

void vitals_ai_step(int16_t heart_rate,int16_t spo2,bool valid,vitals_ai_result_t *r) {
  if(!r)return;
  r->models_ready=true;r->hard_limit=false;
  if(!valid){r->level=0;r->history_ready=false;r->ai_anomaly=false;return;}
  float hr=heart_rate,oxygen=spo2;
  if(baseline_count<BASELINE_SAMPLES&&hr>=51&&hr<=100&&oxygen>=95){baseline_values[baseline_count++]=hr;r->hr_baseline=median20(baseline_values,baseline_count);r->spo2_baseline=oxygen;}
  else if(baseline_count>=BASELINE_SAMPLES&&hr>=55&&hr<=100&&oxygen>=96){r->hr_baseline=.995f*r->hr_baseline+.005f*hr;r->spo2_baseline=.995f*r->spo2_baseline+.005f*oxygen;}
  if(recalibrating&&hr>=51&&hr<=100&&oxygen>=95){candidate[candidate_count++]=hr;if(candidate_count==BASELINE_SAMPLES){memcpy(baseline_values,candidate,sizeof(candidate));baseline_count=BASELINE_SAMPLES;r->hr_baseline=median20(candidate,BASELINE_SAMPLES);r->spo2_baseline=oxygen;recalibrating=false;recalibration_done=true;}}
  r->baseline_samples=baseline_count;
  hr_window[window_head]=hr;spo2_window[window_head]=oxygen;window_head=(uint8_t)((window_head+1U)%WINDOW);if(window_count<WINDOW)window_count++;
  r->history_samples=window_count;r->history_ready=window_count==WINDOW;
  if(!r->history_ready){r->level=1;r->ai_anomaly=false;r->hr_forecast_16s=hr;r->spo2_forecast_16s=oxygen;return;}
  float h[WINDOW],o[WINDOW],f[14],hm,om,hmin=999,hmax=0,omin=999;ordered(h,o);hm=mean(h,WINDOW);om=mean(o,WINDOW);
  for(uint8_t i=0;i<WINDOW;i++){if(h[i]<hmin)hmin=h[i];if(h[i]>hmax)hmax=h[i];if(o[i]<omin)omin=o[i];}
  f[0]=hm;f[1]=stddev(h,WINDOW,hm);f[2]=hmin;f[3]=hmax;f[4]=h[19];f[5]=slope(h,WINDOW);f[6]=h[19]-(r->hr_baseline>0?r->hr_baseline:86.0f);
  f[7]=om;f[8]=stddev(o,WINDOW,om);f[9]=omin;f[10]=o[19];f[11]=slope(o,WINDOW);f[12]=h[19]-h[15];f[13]=o[19]-o[15];
  float baseline=r->hr_baseline>0?r->hr_baseline:86.0f;uint8_t predicted=conservative(tree(f),f,baseline);r->level=majority_vote(predicted);
  r->hr_forecast_16s=h[19]+fmaxf(-12.0f,fminf(12.0f,5.0f*f[5]));r->spo2_forecast_16s=o[19]+fmaxf(-2.5f,fminf(2.5f,5.0f*f[11]));
  r->ai_anomaly=r->level>1U;r->hard_limit=hr<=35||hr>=140||oxygen<=90;r->anomaly_score_x100=r->level==3U?200.0f:r->level==2U?100.0f:0.0f;(void)test_mode;
}
