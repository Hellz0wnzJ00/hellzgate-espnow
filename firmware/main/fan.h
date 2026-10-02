// fan switch
// the fullgate board switches the fan with an AP22811 load switch, so all the
// firmware does is drive its enable pin. high is on

#ifndef FAN_H
#define FAN_H

void fan_init(void);
void fan_set(int on);
int fan_on(void);

#endif
