/*
* This is more or less a copy of stevens code
* with slight modifications: 
* - rtt is store in microseconds instead of seconds
* - All floats have been reduced to unsigned 32 bit integers 
* - Except base which is a 64 bit long, designed to hold the current value in milliseconds
* - RTT RXTMIN and RXTMAX have been changed to 1000 and 3000 ms respectivly
*/

#ifndef __MYRTT
#define __MYRTT

#define RTT_RXTMIN 1000
#define RTT_RXTMAX 3000
#define RTT_MAXNREXMT 12
#define	RTT_RTOCALC(ptr) ((ptr)->rtt_srtt + (4.0 * (ptr)->rtt_rttvar))

typedef struct {
	uint32_t rtt_rtt;
	uint32_t rtt_srtt;
	uint32_t rtt_rttvar;
	uint32_t rtt_rto;
	uint32_t rtt_nrexmt;
	long long rtt_base;
} rtt_info;

uint32_t rtt_minmax(uint32_t rto) {
	if (rto < RTT_RXTMIN)
		rto = RTT_RXTMIN;
	else if (rto > RTT_RXTMAX)
		rto = RTT_RXTMAX;
	return rto;
}

void rtt_init(rtt_info *ptr) {
	struct timeval tv;
	Gettimeofday(&tv, NULL);
	ptr->rtt_base = (tv.tv_sec * 1000) + (tv.tv_usec * .001);
	ptr->rtt_rtt    = 0;
	ptr->rtt_srtt   = 0;
	ptr->rtt_rttvar = 750;
	ptr->rtt_rto = rtt_minmax(RTT_RTOCALC(ptr));
}

uint32_t rtt_ts(rtt_info *ptr) {
	float ts;
	struct timeval tv;
	Gettimeofday(&tv, NULL);
	ts = (tv.tv_sec * 1000) - ptr->rtt_base;
	ts *= 1000;
	ts += tv.tv_usec;
	return (uint32_t)ts;
}

void rtt_newpack(rtt_info *ptr) {
	ptr->rtt_nrexmt = 0;
}

uint32_t rtt_start(rtt_info *ptr) {
	return ptr->rtt_rto;		
}

void rtt_stop(rtt_info *ptr, uint32_t ms) {
	uint32_t delta;
	ptr->rtt_rtt = ms;
	delta = ptr->rtt_rtt - ptr->rtt_srtt;
	delta = delta >> 3;
	ptr->rtt_srtt += delta;

	if(delta < 0)
		delta = -delta;

	delta = delta - ptr->rtt_rttvar;
	delta = delta >> 2;

	ptr->rtt_rttvar += delta;
	ptr->rtt_rto = rtt_minmax(RTT_RTOCALC(ptr));
}

uint32_t rtt_timeout(rtt_info *ptr) {
	ptr->rtt_rto = ptr->rtt_rto << 1;
	ptr->rtt_rto = rtt_minmax(ptr->rtt_rto);
	if (++ptr->rtt_nrexmt > RTT_MAXNREXMT)
		return -1;
	return 0;
}

#endif
