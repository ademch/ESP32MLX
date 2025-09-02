
#ifndef CALIBRATION_H
#define CALIBRATION_H

int  read_user_mlx_calibration_offsets();
void read_user_mlx_calibration_date(char* strDate);
int  write_user_mlx_calibration_offsets(const char* httpDate);
void clear_user_mlx_calibration_offsets();

#endif
