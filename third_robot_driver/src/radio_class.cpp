#include "radio_class.hpp"
using namespace std;

radio_class::radio_class(    
			 std::string new_serial_port_imcs01, 
			 int         new_baudrate_imcs01,
			 std::string new_serial_port_arduino, 
			 int         new_baudrate_arduino)
{
  imcs01_port_name = new_serial_port_imcs01;
  fd_imcs01 = -1;
  baudrate_imcs01 = new_baudrate_imcs01;

  delta_rear_encoder_counts = -1;
  steer_angle = 0.0;

  last_rear_encoder_counts = 0;
  last_rear_encoder_time = 0;

  stasis_ = ROBOT_STASIS_FORWARD_STOP;//forward mode but stopping

  resetOdometry();
}

radio_class::~radio_class()
{
  //  cout << "Destructor called\n" << endl;
  cmd_ccmd.offset[0] = 65535; // iMCs01 CH101 PIN2 is 5[V]. Forwarding flag.
  cmd_ccmd.offset[1] = 32767; // STOP
  ioctl(fd_imcs01, URBTC_COUNTER_SET);
  write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd));

  //! iMCs01
  if(fd_imcs01 > 0){
    tcsetattr(fd_imcs01, TCSANOW, &oldtio_imcs01);
    close(fd_imcs01);
  }
  fd_imcs01 = -1;
}

int radio_class::openSerialPort()
{
  try{ setSerialPort(); }
  catch(exception &e){
	cerr << e.what() << endl;
	return -1; 
  }
  return 0;
}

int radio_class::setSerialPort()
{
  // Setting iMCs01
  if(fd_imcs01 > 0){
    throw logic_error("imcs01 is already open");
  }
  fd_imcs01 = open(imcs01_port_name.c_str(), O_RDWR);
  if( fd_imcs01 > 0){
    cout << imcs01_port_name << endl;
    tcgetattr(fd_imcs01, &oldtio_imcs01);
  }else{
    throw logic_error("Faild to open port: imcs01");
  }
  if(ioctl(fd_imcs01, URBTC_CONTINUOUS_READ) < 0){
    throw logic_error("Faild to ioctl: URBTC_CONTINUOUS_READ");
  }

  cmd_ccmd.selout     = SET_SELECT | CH0 | CH1 | CH2 | CH3; // All PWM.
  cmd_ccmd.selin      = SET_SELECT; // All input using for encoder count.
  cmd_ccmd.setoffset  = CH0 | CH1 | CH2 | CH3;
  cmd_ccmd.offset[0]  = 58981;
  cmd_ccmd.offset[1]  = 58981;
  cmd_ccmd.offset[2]  = 58981;
  cmd_ccmd.offset[3]  = 58981;	// 1/2
  cmd_ccmd.setcounter = CH0 | CH1 | CH2 | CH3;
  // cmd_ccmd.counter[1] = 29134;	//32767-67[deg]*(1453/27), initialize.
  cmd_ccmd.counter[1] = -3633;	//-67[deg]*(1453/27), initialize.
  cmd_ccmd.counter[2] = 0;
  cmd_ccmd.posneg     = SET_POSNEG | CH0 | CH1 | CH2 | CH3; //POS PWM out.
  cmd_ccmd.breaks     = SET_BREAKS | CH0 | CH1 | CH2 | CH3; //No Brake;
  cmd_ccmd.magicno    = 0x00;

  if(ioctl(fd_imcs01, URBTC_COUNTER_SET) < 0){
    throw logic_error("Faild to ioctl: URBTC_COUNTER_SET");
  }
  if(write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd)) < 0){
    throw logic_error("Faild to write");
  }

  cmd_ccmd.setcounter = 0;
  cout << "ThirdRobotInterface (iMCs01)Connected to : " << imcs01_port_name << endl;
  
  return 0;
}

int radio_class::closeSerialPort()
{
  drive(0.0, 0.0);
  usleep(1000);

  if(fd_imcs01 > 0){
    tcsetattr(fd_imcs01, TCSANOW, &oldtio_imcs01);
    close(fd_imcs01);
    fd_imcs01 = -1;
  }
  return 0;
}

// Set the speeds
//   linear_speed  : target linear speed[m/s]
//   angular_speed : target angular speed[rad/s]

int radio_class::radio_drive(double linear_speed)
{
  if(linear_speed >0) {
    if( runmode != FORWARD_MODE){

      cmd_ccmd.offset[0] = 0xffff;
      cmd_ccmd.offset[1] = 0x7fff;// Back is constant speed.
      if(ioctl(fd_imcs01, URBTC_COUNTER_SET) < 0){ return (-1); }
      if(write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd)) < 0){ return (-1); }
      stasis_ = ROBOT_STASIS_FORWARD;
      runmode = FORWARD_MODE;
      sleep(1);
      //----------------------------------------------------------------

    }else{
      //----------------------------------------------------------------
      //forward_mode 
      //imcs01 CH101 pin2(PWM) 5[V].Forwarding flag.
      //imcs01 CH102 pin2(PWM) 5[V]
      //----------------------------------------------------------------
      cmd_ccmd.offset[0] = 0xffff;
      cmd_ccmd.offset[1] = 60000;
      if(ioctl(fd_imcs01, URBTC_COUNTER_SET) < 0){ return (-1); }
      if(write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd)) < 0){ return (-1); }
      stasis_ = ROBOT_STASIS_FORWARD;
      runmode = FORWARD_MODE;
      //----------------------------------------------------------------

    }
  } else if(linear_speed < 0) {
    if(runmode != BACK_MODE){

      cmd_ccmd.offset[0] = 0x7fff;
      cmd_ccmd.offset[1] = 0x7fff;// Back is constant speed.
      if(ioctl(fd_imcs01, URBTC_COUNTER_SET) < 0){ return (-1); }
      if(write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd)) < 0){ return (-1); }
      stasis_ = ROBOT_STASIS_BACK;
      runmode = BACK_MODE;
      sleep(1);
      //----------------------------------------------------------------

    }else{
      //----------------------------------------------------------------
      //back_mode
      // iMCs01 CH101 PIN2 is 0[V]. Backing flag.
      // iMCs01 CH102 PIN2 is 5[V].
      //----------------------------------------------------------------
      cmd_ccmd.offset[0] = 0x7fff;
      cmd_ccmd.offset[1] = 60000;// Back is constant speed.
      if(ioctl(fd_imcs01, URBTC_COUNTER_SET) < 0){ return (-1); }
      if(write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd)) < 0){ return (-1); }
      stasis_ = ROBOT_STASIS_BACK;
      runmode = BACK_MODE;
      //----------------------------------------------------------------

    }
  } else {

    //----------------------------------------------------------------
    //back_mode
    // iMCs01 CH101 PIN2 is 0[V]. Backing flag.
    // iMCs01 CH102 PIN2 is 0[V]. 
    //----------------------------------------------------------------
    cmd_ccmd.offset[1] = 0x7fff;// Back is constant speed.
    if(ioctl(fd_imcs01, URBTC_COUNTER_SET) < 0){ return (-1); }
    if(write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd)) < 0){ return (-1); }
    if(stasis_ == ROBOT_STASIS_BACK){ stasis_ = ROBOT_STASIS_BACK_STOP;}
    if(stasis_ == ROBOT_STASIS_FORWARD_STOP){ stasis_ = ROBOT_STASIS_BACK_STOP;}
    //----------------------------------------------------------------
    
  }

  cout         <<
    "CH101:  " <<   cmd_ccmd.offset[0] << "CH102:  " <<cmd_ccmd.offset[1]
	       <<   endl;
}

  int radio_class::drive(
			 double linear_speed,
			 double angular_speed)
  {
    // Front angle in deg.
    double front_angle_deg = 0;
    if(linear_speed == 0.0){
      front_angle_deg = 0;
      // その場旋回を要求してきたら後退する。
      if(fabs(angular_speed) > 0.0){
	double rear_speed_m_s = -0.5;
	return driveDirect(front_angle_deg, rear_speed_m_s);
      }
    }else{
      front_angle_deg = ((atan((WHEELBASE_LENGTH*angular_speed)/linear_speed))*(180/M_PI));
      //cout << "front_angle_deg : " << front_angle_deg << endl;
    }
    // Rear wheel velocity in [m/s]
    double rear_speed_m_s = linear_speed;
  
    //cout << front_angle_deg << rear_speed_m_s << endl;
    if(front_angle_deg==0){ 
      front_angle_deg = 1.0;
    }


    return driveDirect(front_angle_deg, rear_speed_m_s);
  }

  // Set the motor speeds
  //   front_angular : target angle[deg]
  //   rear_speed    : target velocity[m/s]
  //   stasis_ : 
  //     ROBOT_STASIS_FORWARD      : Forwarding
  //     ROBOT_STASIS_FORWARD_STOP : Stoping but forward mode
  //     ROBOT_STASIS_BACK         : Backing
  //     ROBOT_STASIS_BACK_STOP    : Stoping but back mode
  //     ROBOT_STASIS_OTHERWISE    : Braking but not stop.

  int radio_class::driveDirect(
			       double front_angular,
			       double rear_speed)
  {
    static int forward_stop_cnt = 0;
    static int back_stop_cnt = 0;

    static double u  = 0x7fff;//32767.0;
    static double u1 = 0x7fff;//32767.0;
    static double u2 = 0x7fff;//32767.0;
    static double e  = 0;
    static double e1 = 0;
    static double e2 = 0;

    static double gain_p = 10000.0;
    static double gain_i = 10000.0;
    static double gain_d = 1000.0;

    double duty = 0;

    if(rear_speed >= 0.0){	// Forward
      double rear_speed_m_s = MIN(rear_speed, MAX_LIN_VEL); // return smaller
      if(stasis_ == ROBOT_STASIS_FORWARD || stasis_ == ROBOT_STASIS_FORWARD_STOP){ // Now Forwarding
	//------------------------------------------------------
	e = rear_speed_m_s - linear_velocity;
	//cout << "e:" << e << endl;
	u = u1 + (gain_p + gain_i * delta_rear_encoder_time + gain_d/delta_rear_encoder_time) * e 
	  - (gain_p + 2.0*gain_d/delta_rear_encoder_time)*e1 + (gain_d/delta_rear_encoder_time)*e2;
	//------------------------------------------------------------

	//cout << "test-2:" << u << endl;
	if(rear_speed == 0.0){ u = 0x7fff;}//32767; }
	duty = MIN(u, 60000);

	duty = MAX(duty, 0x7fff);
	u2 = u1; u1 = duty; e2 = e1; e1 = e;


	cmd_ccmd.offset[0] = 0xffff;//65535; // iMCs01 CH101 PIN2 is 5[V]. Forwarding flag.
	//  cmd_ccmd.offset[1] = (int)(32767.0 + 29409.0*(rear_speed_m_s/MAX_LIN_VEL));
	cmd_ccmd.offset[1] = (int)(duty);


	cout << "duty :" << duty << endl;
	runmode = FORWARD_MODE;

	if(ioctl(fd_imcs01, URBTC_COUNTER_SET) < 0){ return (-1); }
	if(write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd)) < 0){ return (-1); }
	stasis_ = ROBOT_STASIS_FORWARD;
	//cout << "test1:" << cmd_ccmd.offset[1] << endl;
      } else { // Now Backing
	// Need to stop once.

	cmd_ccmd.offset[0] = 0xffff;//65535; // iMCs01 CH101 PIN2 is 5[V]. Forwarding flag.
	cmd_ccmd.offset[1] = 0x7fff;//32767; // STOP


	runmode = FORWARD_STOP_MODE;
	u = 0x7fff;//32767;
	duty = u;
	u2 = u1; u1 = duty; e2 = e1; e1 = e;

	if(ioctl(fd_imcs01, URBTC_COUNTER_SET) < 0){ return (-1); } // error
	if(write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd)) < 0){ return (-1); }
	//cout <<  "test2" << cmd_ccmd.offset[1] << endl;
	if(forward_stop_cnt >= 20){
	  stasis_ = ROBOT_STASIS_FORWARD_STOP;
	  forward_stop_cnt = 0;
	}else{
	  stasis_ = ROBOT_STASIS_OTHERWISE;
	  forward_stop_cnt++;
	}
      }
    }else{ // (rear_speed < 0) -> Back
      if(stasis_ == ROBOT_STASIS_BACK_STOP || stasis_ == ROBOT_STASIS_BACK){ // Now backing


	cmd_ccmd.offset[0] = 0x7fff;//32767; // iMCs01 CH101 PIN2 is 0[V]. Backing flag.
	cmd_ccmd.offset[1] = 60000; // Back is constant speed.
	runmode = BACK_MODE;

	u = 0x7fff;//32767;
	duty = MIN(u, 62176);
	u2 = u1; u1 = duty; e2 = e1; e1 = e;

	if(ioctl(fd_imcs01, URBTC_COUNTER_SET) < 0){ return (-1); }
	if(write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd)) < 0){ return (-1); }
	//cout <<  "test3" << cmd_ccmd.offset[1] << endl;
	stasis_ = ROBOT_STASIS_BACK;
      }else{ // Now forwarding

	cmd_ccmd.offset[0] = 0x7fff;//32767; // iMCs01 CH101 PIN2 is 0[V].  Backing flag.
	cmd_ccmd.offset[1] = 0x7fff;//32767; // STOP

	runmode = BACK_STOP_MODE;
	if(ioctl(fd_imcs01, URBTC_COUNTER_SET) < 0){ return (-1); }
	if(write(fd_imcs01, &cmd_ccmd, sizeof(cmd_ccmd)) < 0){ return (-1);}
	//	  cout <<  "test4" << cmd_ccmd.offset[1] << endl;
	if(back_stop_cnt >= 20){
	  stasis_ = ROBOT_STASIS_BACK_STOP;
	  back_stop_cnt = 0;
	}else{
	  stasis_ = ROBOT_STASIS_OTHERWISE;
	  back_stop_cnt++;
	}
      }
    }
  }

  // *****************************************************************************
  // Read the encoders from iMCs01
  int radio_class::getEncoderPacket()
  {
    if(read(fd_imcs01, &cmd_uin, sizeof(cmd_uin)) != sizeof(cmd_uin)){
      return -1;
    }else{
      return parseEncoderPackets();
    }
  }

  // *****************************************************************************
  // Parse encoder data
  int radio_class::parseEncoderPackets()
  {
    parseFrontEncoderCounts();
    parseRearEncoderCounts();
    return 0;
  }

  int radio_class::parseFrontEncoderCounts()
  {
    const int ave_num = 5;
    static int cnt = 0;
    static vector<double> move_ave(ave_num);

    int steer_encoder_counts = (int)(cmd_uin.ct[1]);

    double alpha = 0.0;
    double beta = 0.0;
    double R = 0.0;
    const double n = 1.00;

    if(steer_encoder_counts == 0.0){
      steer_angle = 0.0;
    }else{
      double tmp = steer_encoder_counts*67.0/3633.0;
      tmp = tmp * M_PI /180;
      R = 0.96/tan(tmp);

      steer_angle = atan(WHEELBASE_LENGTH/R); // [rad]

      steer_angle = steer_angle*(180.0/M_PI); // [rad]->[deg]
    }
  
    move_ave[cnt%5] = steer_angle;
    size_t size = move_ave.size();
    double sum = 0;
    for(unsigned int i = 0; i < size; i++){
      sum += move_ave[i];
    }
    steer_angle = sum / (double)size;
    cnt++;

    return 0;
  }

  int radio_class::parseRearEncoderCounts()
  {
    int rear_encoder_counts = (int)(cmd_uin.ct[2]);

    delta_rear_encoder_time = (double)(cmd_uin.time) - last_rear_encoder_time;
    if(delta_rear_encoder_time < 0){
      delta_rear_encoder_time = 65535 - (last_rear_encoder_time - cmd_uin.time);
    }
    delta_rear_encoder_time = delta_rear_encoder_time / 1000.0; // [ms] -> [s]
    last_rear_encoder_time = (double)(cmd_uin.time);
    
    if(delta_rear_encoder_counts == -1 
       || rear_encoder_counts == last_rear_encoder_counts){ // First time.

      delta_rear_encoder_counts = 0;

    }else{
      delta_rear_encoder_counts = rear_encoder_counts - last_rear_encoder_counts;

      // checking imcs01 counter overflow.
      if(delta_rear_encoder_counts > ROBOT_MAX_ENCODER_COUNTS/10){
	delta_rear_encoder_counts = delta_rear_encoder_counts - ROBOT_MAX_ENCODER_COUNTS;
      }
      if(delta_rear_encoder_counts < -ROBOT_MAX_ENCODER_COUNTS/10){
	delta_rear_encoder_counts = delta_rear_encoder_counts + ROBOT_MAX_ENCODER_COUNTS;
      }

    }
    last_rear_encoder_counts = rear_encoder_counts;
    return 0;
  }

  void radio_class::setOdometry(
				double new_x,
				double new_y,
				double new_yaw)
  {
    odometry_x_   = new_x;
    odometry_y_   = new_y;
    odometry_yaw_ = new_yaw;
  }

  void radio_class::resetOdometry()
  {
    setOdometry(0.0, 0.0, 0.0);
  }

  // *****************************************************************************
  // Calculate Third Robot odometry
  void radio_class::calculateOdometry()
  {
    // 0.06005
    double dist = (delta_rear_encoder_counts/4.0*26.0/20.0)*(0.06505*M_PI/360.0);// pulse to meter
    linear_velocity = dist / delta_rear_encoder_time;


    //どうもエンコーダの距離が出すぎているのでここで調整
    dist = dist * 0.90;
	
    //    double ang = sin((steer_angle*M_PI)/180.0)/WHEELBASE_LENGTH;//steer_angle is [deg]
    
    double ang = tan((steer_angle*M_PI)/180.0)/WHEELBASE_LENGTH;//steer_angle is [deg] // もしかしてこっちなのでは
    if(ang == 0.0){
      odometry_x_ = odometry_x_ + dist * cos(odometry_yaw_);// [m]
      odometry_y_ = odometry_y_ + dist * sin(odometry_yaw_);// [m]
      odometry_yaw_ = odometry_yaw_;        //[rad]
    }else{
      odometry_x_ = odometry_x_ + (sin(odometry_yaw_+dist*ang)-sin(odometry_yaw_))/ang;//[m]
      odometry_y_ = odometry_y_ - (cos(odometry_yaw_+dist*ang)-cos(odometry_yaw_))/ang;//[m]
      odometry_yaw_ = NORMALIZE(odometry_yaw_ + dist * ang);//[yaw]
    }
  }


  int plus_or_minus(double value){
    if(value > 0){
      return 1;
    }else if(value < 0){
      return -1;
    }else{
      return 0;
    }
  }
