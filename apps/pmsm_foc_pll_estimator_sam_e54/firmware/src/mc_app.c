/*******************************************************************************
 Motor Control Application Source File

  File Name:
    mc_app.c

  Summary:
 Motor Control Application Variable and Function definitions.

  Description:
    This file contains the variable initializations and function definitions for
 *  specific to Motor Control Application (excluding variables and functions 
 *  defined by Motor ControlLibrary
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *******************************************************************************/
// DOM-IGNORE-END

#include "userparams.h"
#include <math.h>
#include "mc_app.h"
#include "mc_Lib.h"
#include "definitions.h"


mcParam_PIController     			mcApp_Q_PIParam;      // Parameters for Q axis Current PI Controller 
mcParam_PIController     			mcApp_D_PIParam;      // Parameters for D axis Current PI Controller 
mcParam_PIController     			mcApp_Speed_PIParam;  // Parameters for Speed PI Controller 
mcParam_FOC							mcApp_focParam;       // Parameters related to Field Oriented Control
mcParam_SinCos					    mcApp_SincosParam;    // Parameters related to Sine/Cosine calculator
mcParam_SVPWM 						mcApp_SVGenParam;     // Parameters related to Space Vector PWM
mcParam_ControlRef 					mcApp_ControlParam;   // Parameters related to Current and Speed references
mcParam_PLLEstimator                mcApp_EstimParam;     // Parameters related to PLL Estimator
mcParam_AlphaBeta                   mcApp_I_AlphaBetaParam; // Alpha and Beta (2 Phase Stationary Frame) axis Current values
mcParam_AlphaBeta                   mcApp_IRef_AlphaBetaParam; // Alpha and Beta (2 Phase Stationary Frame) axis Current Reference values
mcParam_DQ                          mcApp_I_DQParam;// D and Q axis (2 Phase Rotating Frame) current values
mcParam_DQ                          mcApp_IRef_DQParam; // D and Q axis (2 Phase Rotating Frame) Current Reference Values
mcParam_ABC                         mcApp_I_ABCParam; // A,B,C axis (3 Phase Stationary Frame) current values
mcParam_AlphaBeta                   mcApp_V_AlphaBetaParam; // Alpha and Beta (2 Phase Stationary Frame) axis voltage values
mcParam_DQ                          mcApp_V_DQParam;// D and Q axis (2 Phase Rotating Frame) voltage values
motor_status_t                      mcApp_motorState;
delay_gen_t                         delay_10ms;
state_machine_status_t              motor_state_machine;
float 								OpenLoop_Ramp_Angle_Rads_Per_Sec = 0; 	// ramp angle variable for initial ramp 
unsigned int 						Align_Counter = 0; 				// lock variable for initial ramp 
unsigned int                        Windmilling_Counter = 0;
unsigned int                        Passive_Brake_Counter = 0;
short        						potReading;
int16_t                             phaseCurrentA;
int16_t                             phaseCurrentB;
float								DoControl_Temp1, DoControl_Temp2;

float                               d_intermediate;
mcParam_DQ                          I_dq_old,I_dq_new;
float                               ol_angle,cl_angle;
uint16_t calibration_sample_count = 0x0000U;
uint16_t adc_0_offset = 0;
uint16_t adc_1_offset = 0;
uint32_t adc_0_sum = 0;
uint32_t adc_1_sum = 0;
uint32_t curpi_counter = 0;
float invDutyA = 0;
float invDutyB = 0;
float invDutyC = 0;

uint8_t  overCurrentFaultActive = 0;
/*initializing the fault delay counter to final value to avoid any overcurrent 
reset delay in case an OC fault is triggered upon board power up*/
uint32_t    overCurrentFaultResetDelayCounter = OVERCURRENT_RESET_DELAY_COUNT;
char    state_count = 0;

void mcApp_SpeedRamp()
{
        if(mcApp_motorState.motorDirection ==0)
        {
            mcApp_ControlParam.VelInput = (float)((float)potReading * POT_ADC_COUNT_FW_SPEED_RATIO);
        }
        else
        {
             mcApp_ControlParam.VelInput = (float)((float)-potReading * POT_ADC_COUNT_FW_SPEED_RATIO); 
        }
        mcApp_ControlParam.Diff =  mcApp_ControlParam.VelInput - mcApp_ControlParam.VelRef;
    
      
       //Speed Rate Limiter implementation.    
        if(mcApp_ControlParam.Diff >=CLOSEDLOOP_SPEED_HYSTERESIS)
        {
             mcApp_ControlParam.VelRef+=CLOSEDLOOP_SPEED_RAMP_RATE_DELTA;   
        }
        else if(mcApp_ControlParam.Diff <=-CLOSEDLOOP_SPEED_HYSTERESIS)
        {
             mcApp_ControlParam.VelRef-=CLOSEDLOOP_SPEED_RAMP_RATE_DELTA; 
        }
        else
        {
             mcApp_ControlParam.VelRef = mcApp_ControlParam.VelInput;
        }
         if(mcApp_motorState.motorDirection ==0)
        {
            if(mcApp_ControlParam.VelRef < OPENLOOP_END_SPEED_RADS_PER_SEC_ELEC)
            {
                mcApp_ControlParam.VelRef = OPENLOOP_END_SPEED_RADS_PER_SEC_ELEC;
            }
            else
            {
                
            }
        }
        else
        {
            if(mcApp_ControlParam.VelRef > -OPENLOOP_END_SPEED_RADS_PER_SEC_ELEC)
            {
                mcApp_ControlParam.VelRef = -OPENLOOP_END_SPEED_RADS_PER_SEC_ELEC;
            } 
            else
            {
                
            }
        }
		
        
}

void PWM_Output_Disable()
{
    TCC0_PWM24bitDutySet(TCC0_CHANNEL0,(uint32_t)PWM_PERIOD_COUNT>>1);
    TCC0_PWM24bitDutySet(TCC0_CHANNEL1,(uint32_t)PWM_PERIOD_COUNT>>1);
    TCC0_PWM24bitDutySet(TCC0_CHANNEL2,(uint32_t)PWM_PERIOD_COUNT>>1);
    
    /*Override all PWM outputs to low*/
    TCC0_PWMPatternSet((TCC_PATT_PGE0_Msk|TCC_PATT_PGE1_Msk|TCC_PATT_PGE2_Msk
            |TCC_PATT_PGE4_Msk|TCC_PATT_PGE5_Msk|TCC_PATT_PGE6_Msk),
            (TCC_PATT_PGE0(0)|TCC_PATT_PGE1(0)|TCC_PATT_PGE2(0)|TCC_PATT_PGE4(0)
            |TCC_PATT_PGE5(0)|TCC_PATT_PGE6(0)));
}

void PWM_Output_Enable()
{
    TCC0_PWMPatternSet(0x00,0x00);/*Disable PWM override*/
}

/* This ISR calibrates zero crossing point for Phase U and Phase V currents*/
void ADC_CALIB_ISR (ADC_STATUS status, uintptr_t context)
{
    X2CScope_Update();
    calibration_sample_count++;
    if(calibration_sample_count <= 4096)
    {
        adc_0_sum += ADC0_ConversionResultGet();    
        adc_1_sum += ADC1_ConversionResultGet();
    }
    else
    {
        adc_0_offset = adc_0_sum>>12;
        adc_1_offset = adc_1_sum>>12;
        ADC0_Disable();
        ADC0_CallbackRegister((ADC_CALLBACK) mcApp_ADCISRTasks, (uintptr_t)NULL);
        ADC0_Enable();
    }
 
}
// *****************************************************************************
// *****************************************************************************
// Section: MC ADC ISR TASKS
// *****************************************************************************
// *****************************************************************************
void mcApp_ADCISRTasks(ADC_STATUS status, uintptr_t context)
{
	X2CScope_Update();
    phaseCurrentA = (int16_t)ADC0_ConversionResultGet() - (int16_t)adc_0_offset;// Phase Current A measured using ADC0
    phaseCurrentB = (int16_t)ADC1_ConversionResultGet() - (int16_t)adc_1_offset;// Phase Current B measured using ADC4
    
    /* Clear all interrupt flags */
       ADC0_REGS->ADC_INTFLAG = ADC_INTFLAG_Msk;

       ADC0_REGS->ADC_INTENCLR = ADC_INTFLAG_RESRDY_Msk;// Disable ADC interrupt
    /* select the next channel */
	/* select the next ADC channel for conversion */
    ADC0_ChannelSelect(ADC_POSINPUT_AIN6,ADC_NEGINPUT_GND); // Potentiometer to ADC0
    ADC1_ChannelSelect(ADC_POSINPUT_AIN14,ADC_NEGINPUT_GND); // DC Bus Voltage to ADC1
    ADC0_REGS->ADC_SWTRIG |= ADC_SWTRIG_START_Msk; 
    
   
    
if(mcApp_motorState.focStart)
{
    mcApp_I_ABCParam.a = (float)phaseCurrentA*ADC_CURRENT_SCALE * (-1); 
    mcApp_I_ABCParam.b = (float)phaseCurrentB*ADC_CURRENT_SCALE * (-1);
    
    mcLib_ClarkeTransform(&mcApp_I_ABCParam, &mcApp_I_AlphaBetaParam);
   
    
    
    mcLib_ParkTransform(&mcApp_I_AlphaBetaParam, &mcApp_SincosParam, 
                        &mcApp_I_DQParam);
#ifndef CURPI_TUN   

    mcLib_PLLEstimator(&mcApp_EstimParam, &mcApp_SincosParam, &mcApp_focParam, 
                       &mcApp_I_AlphaBetaParam, &mcApp_V_AlphaBetaParam);



    switch (mcApp_motorState.focStateMachine)
    {
        case WINDMILLING:
        {
            state_count = 1;
            motor_state_machine.windmilling = state_count;
            mcApp_IRef_DQParam.d = 0;
            mcApp_IRef_DQParam.q = 0;
            if(Windmilling_Counter < COUNT_FOR_WINDMILLING_TIME)
            {
                Windmilling_Counter++;
            }
            else
            {
                mcApp_motorState.focStateMachine = WINDMILLING_DECIDE;
                Windmilling_Counter = 0;
                state_count++;
            }
            
            break;
        }
        
        case WINDMILLING_DECIDE:
        {
            motor_state_machine.windmilling_decide = state_count;
            if(mcApp_EstimParam.qVelEstim > MIN_WM_SPEED_SPEED_ELEC_RAD_PER_SEC) 
            {
                if(mcApp_motorState.motorDirection == 0)
                {
                    mcApp_Speed_PIParam.qdSum = WINDMILL_START_Q_AXIS_REF;
                    mcApp_ControlParam.VelRef = mcApp_EstimParam.qVelEstim;
                    mcApp_D_PIParam.qInRef = 0.0;
                    mcApp_IRef_DQParam.d = 0.0;
                    mcApp_motorState.focStateMachine = CLOSEDLOOP_FOC;
                    state_count++;
                }
                else
                {
                    if(mcApp_IRef_DQParam.q >  -REGEN_BRAKE_CURRENT_REF)
                    {
                        mcApp_IRef_DQParam.q -= REGEN_BRAKE_CURRENT_STEP;
                    }
                    else
                    {
                        mcApp_IRef_DQParam.q = -REGEN_BRAKE_CURRENT_REF;
                    }
                    mcApp_motorState.focStateMachine = WINDMILLING_DECIDE;
                    motor_state_machine.regen_brake = state_count;
                }
                
            }
            else if (mcApp_EstimParam.qVelEstim < -MIN_WM_SPEED_SPEED_ELEC_RAD_PER_SEC)
            {
                if(mcApp_motorState.motorDirection == 1)
                {
                    mcApp_Speed_PIParam.qdSum = -WINDMILL_START_Q_AXIS_REF;
                    mcApp_ControlParam.VelRef = -mcApp_EstimParam.qVelEstim;
                    mcApp_D_PIParam.qInRef = 0.0;
                    mcApp_IRef_DQParam.d = 0.0;
                    mcApp_motorState.focStateMachine = CLOSEDLOOP_FOC;
                    state_count++;
                }
                else
                {
                    if(mcApp_IRef_DQParam.q <  REGEN_BRAKE_CURRENT_REF)
                    {
                        mcApp_IRef_DQParam.q += REGEN_BRAKE_CURRENT_STEP;
                    }
                    else
                    {
                        mcApp_IRef_DQParam.q = REGEN_BRAKE_CURRENT_REF;
                    }
                    mcApp_motorState.focStateMachine = WINDMILLING_DECIDE;
                    motor_state_machine.regen_brake = state_count;
                }
            }
            else
            {
                mcApp_motorState.focStateMachine = WINDMILLING_PASSIVE_BRAKE;
                state_count++;
            }
            
            break;
        }
        
        case WINDMILLING_PASSIVE_BRAKE:
        {
            motor_state_machine.passive_brake = state_count;
            if(Passive_Brake_Counter < COUNT_FOR_PASSIVE_BRAKE_TIME)
            {
                Passive_Brake_Counter++;
                
            }
            else
            {
                mcApp_motorState.focStateMachine = ALIGN;
                
                state_count++;
            }
            mcApp_IRef_DQParam.d = mcApp_IRef_DQParam.q = 0;
            //Not resetting the PI Controller's integral term can cause OC fault in Long hurst motor.
            mcApp_D_PIParam.qdSum = 0;
            mcApp_Q_PIParam.qdSum = 0;
            mcApp_ControlParam.AssertActiveVector = 0;
            break;
         }
                  
    
            
                
        case ALIGN:
        {
         motor_state_machine.align = state_count;
         mcApp_ControlParam.AssertActiveVector = 1;
         mcApp_ControlParam.ol_cl_complete = 0;
         if (Align_Counter < COUNT_FOR_ALIGN_TIME)
            {
                     
            Align_Counter++;
            mcApp_motorState.focStateMachine = ALIGN;
            } 
        else 
           {
            OpenLoop_Ramp_Angle_Rads_Per_Sec = 0;   
            mcApp_focParam.OpenLoopAngle = 0;
            Align_Counter = 0;
            mcApp_motorState.focStateMachine = OPENLOOP_FOC;
            state_count++;
            }
        /* Align stator reference frame to alpha-beta axis*/
        mcApp_SincosParam.Angle = 0;
        
         if(mcApp_IRef_DQParam.d> ALIGN_D_CURRENT_REF)
         {
            mcApp_IRef_DQParam.d= ALIGN_D_CURRENT_REF;
         }
         else
         {
            mcApp_IRef_DQParam.d+= ALIGN_CURRENT_STEP;
         }
        mcApp_IRef_DQParam.q = 0;

        break;
        }
        
        case OPENLOOP_FOC:
        {
         motor_state_machine.open_loop = state_count;   
         mcApp_ControlParam.AssertActiveVector = 1;
            if (OpenLoop_Ramp_Angle_Rads_Per_Sec < OPENLOOP_END_SPEED_RADS_PER_SEC_ELEC_IN_LOOPTIME)
            {
                OpenLoop_Ramp_Angle_Rads_Per_Sec+=OPENLOOP_RAMPSPEED_INCREASERATE;
            }
        	else // switch to closed loop
            {
            #ifndef OPEN_LOOP_FUNCTIONING
                        mcApp_motorState.focStateMachine = OPENLOOP_TO_CLOSEDLOOP_FOC;
                        
                        if(mcApp_motorState.motorDirection == 0)
                        {
                            mcApp_ControlParam.VelRef = OPENLOOP_END_SPEED_RADS_PER_SEC_ELEC;
                        }
                        else
                        {
                            mcApp_ControlParam.VelRef = -OPENLOOP_END_SPEED_RADS_PER_SEC_ELEC;
                        }

            #endif
            state_count++;
            }
		
            // the angle set depends on startup ramp
            if(mcApp_motorState.motorDirection == 0)
            {
            mcApp_focParam.OpenLoopAngle += OpenLoop_Ramp_Angle_Rads_Per_Sec;
            
            if(mcApp_focParam.OpenLoopAngle >= SINGLE_ELEC_ROT_RADS_PER_SEC)
                mcApp_focParam.OpenLoopAngle = mcApp_focParam.OpenLoopAngle - SINGLE_ELEC_ROT_RADS_PER_SEC;
            }
            else
            {
                mcApp_focParam.OpenLoopAngle -= OpenLoop_Ramp_Angle_Rads_Per_Sec;
            if(mcApp_focParam.OpenLoopAngle <= 0)
                mcApp_focParam.OpenLoopAngle = mcApp_focParam.OpenLoopAngle + SINGLE_ELEC_ROT_RADS_PER_SEC;
            
            }
            /* Force the rotor to start in open loop mode
             * At certain minimum speed, enough BACK EMF is available for PLL 
             * estimator to be able to estimate rotor position */
            mcApp_SincosParam.Angle = mcApp_focParam.OpenLoopAngle;

            mcApp_IRef_DQParam.d= OPENLOOP_D_CURRENT_REF;
            break;
        
        }
        case OPENLOOP_TO_CLOSEDLOOP_FOC:
        {
            motor_state_machine.open_loop_to_closed_loop = state_count;
            mcApp_ControlParam.AssertActiveVector = 1;
            mcApp_SincosParam.Angle = mcApp_focParam.OpenLoopAngle;
            mcLib_SinCosGen(&mcApp_SincosParam);
            
            //Translate Integral values and current reference values of the D and Q axis PI Controllers to new reference frames
            mcApp_V_DQParam.d = mcApp_D_PIParam.qdSum;
            mcApp_V_DQParam.q = mcApp_Q_PIParam.qdSum;
            mcLib_InvParkTransform(&mcApp_V_DQParam,&mcApp_SincosParam, &mcApp_V_AlphaBetaParam);
            mcLib_InvParkTransform(&mcApp_IRef_DQParam,&mcApp_SincosParam, &mcApp_IRef_AlphaBetaParam);
          
            mcApp_SincosParam.Angle = mcApp_EstimParam.qRho;

            mcLib_SinCosGen(&mcApp_SincosParam);
            mcLib_ParkTransform(&mcApp_IRef_AlphaBetaParam, &mcApp_SincosParam, &mcApp_IRef_DQParam);
            mcLib_ParkTransform(&mcApp_V_AlphaBetaParam, &mcApp_SincosParam, &mcApp_V_DQParam);

            mcApp_D_PIParam.qdSum = mcApp_V_DQParam.d;
            mcApp_Q_PIParam.qdSum = mcApp_V_DQParam.q;
            mcApp_Speed_PIParam.qdSum = mcApp_IRef_DQParam.q;
            
            mcApp_motorState.focStateMachine = CLOSEDLOOP_FOC;
            state_count++;
            break;
        }
        case CLOSEDLOOP_FOC:
        {
            motor_state_machine.closed_loop = state_count;
            mcApp_ControlParam.AssertActiveVector = 1;
            /* Use the rotor angle estimated by PLL estimator as the angle reference for rotating frame*/          
            mcApp_SincosParam.Angle = mcApp_EstimParam.qRho; 
            if(mcApp_ControlParam.ol_cl_complete == 0)
            {
                if(mcApp_motorState.motorDirection == 0)
                {
                    mcApp_ControlParam.VelRef = OPENLOOP_END_SPEED_RADS_PER_SEC_ELEC;
                }
                else
                {
                    mcApp_ControlParam.VelRef = -OPENLOOP_END_SPEED_RADS_PER_SEC_ELEC;
                }
                // calculate max allowable d axis reference with priority to q axis current.
                //In order to ensure that total current vector does not exceed max motor current rating
                d_intermediate =  sqrtf(MAX_MOTOR_CURRENT_SQUARED - (mcApp_IRef_DQParam.q*mcApp_IRef_DQParam.q));

                //If current D axis reference is higher than max allowable D axis reference then limit its value to the max allowable D axis reference.
                if(mcApp_IRef_DQParam.d > d_intermediate)
                {
                    mcApp_IRef_DQParam.d = d_intermediate;
                }

                if(mcApp_IRef_DQParam.d > D_CURRENT_REF_STEP)
                {
                    mcApp_IRef_DQParam.d -= D_CURRENT_REF_STEP;
                }
                else
                {
                    mcApp_IRef_DQParam.d = 0;
                    mcApp_ControlParam.ol_cl_complete = 1;
                }
            }

            //if TORQUE MODE skip the speed and Field Weakening controller               
    #ifndef	TORQUE_MODE
            // Execute the velocity control loop
            mcApp_Speed_PIParam.qInMeas = mcApp_EstimParam.qVelEstim;
            mcApp_Speed_PIParam.qInRef  = mcApp_ControlParam.VelRef;
            mcLib_CalcPI(&mcApp_Speed_PIParam);
            mcApp_IRef_DQParam.q= mcApp_Speed_PIParam.qOut;

    #ifdef ENABLE_FLUX_WEAKENING	
            // Implement Field Weakening if Speed input is greater than the base speed of the motor
            if(mcApp_motorState.motorDirection == 0)
            {
                    if(mcApp_ControlParam.VelRef > NOMINAL_SPEED_RAD_PER_SEC_ELEC && mcApp_ControlParam.ol_cl_complete == 1)
                    {
                        mcApp_focParam.Vds = mcApp_V_DQParam.d*mcApp_V_DQParam.d;

                        if(mcApp_focParam.Vds>MAX_NORM_SQ)
                        {
                        mcApp_focParam.Vds = MAX_NORM_SQ;
                        }

                        mcApp_focParam.Vqs = sqrtf(MAX_NORM_SQ-mcApp_focParam.Vds);
                        mcApp_focParam.VqRefVoltage = mcApp_focParam.MaxPhaseVoltage*mcApp_focParam.Vqs;

                         //Calculating Flux Weakening value of Id, Id_flux_Weakening = (Vqref- Rs*Iq - BEMF)/omega*Ls

                        mcApp_ControlParam.Id_FW_Raw = (mcApp_focParam.VqRefVoltage - (MOTOR_PER_PHASE_RESISTANCE * mcApp_IRef_DQParam.q) 
                                                -(mcApp_ControlParam.VelRef  * MOTOR_BACK_EMF_CONSTANT_Vpeak_PHASE_RAD_PER_SEC_ELEC))/(mcApp_ControlParam.VelRef  * MOTOR_PER_PHASE_INDUCTANCE);

                        mcApp_ControlParam.Id_FW_Filtered = mcApp_ControlParam.Id_FW_Filtered +
                                          ((mcApp_ControlParam.Id_FW_Raw - mcApp_ControlParam.Id_FW_Filtered) * mcApp_ControlParam.qKfilterIdRef) ;	

                        mcApp_IRef_DQParam.d= mcApp_ControlParam.Id_FW_Filtered;
                          //Limit Id such that MAX_FW_NEGATIVE_ID_REF < Id < 0
                        if(mcApp_IRef_DQParam.d> 0)
                            mcApp_IRef_DQParam.d= 0; 

                        if(mcApp_IRef_DQParam.d< MAX_FW_NEGATIVE_ID_REF)
                            mcApp_IRef_DQParam.d= MAX_FW_NEGATIVE_ID_REF;

                        // Limit Q axis current such that sqrtf(Id^2 +Iq^2) <= MAX_MOTOR_CURRENT
                        mcApp_ControlParam.Iqmax = sqrtf((MAX_MOTOR_CURRENT_SQUARED) - (mcApp_IRef_DQParam.d*mcApp_IRef_DQParam.d));	
                    }
                    else
                    {
                        mcApp_IRef_DQParam.d= 0;
                        mcApp_ControlParam.Iqmax = MAX_MOTOR_CURRENT;
                        mcApp_ControlParam.Id_FW_Filtered = 0;
                        mcApp_ControlParam.Id_FW_Raw = 0;
                    }
            }
            else
            {
                    if(mcApp_ControlParam.VelRef < -NOMINAL_SPEED_RAD_PER_SEC_ELEC && mcApp_ControlParam.ol_cl_complete == 1)
                    {
                        mcApp_focParam.Vds = mcApp_V_DQParam.d*mcApp_V_DQParam.d;

                        if(mcApp_focParam.Vds>MAX_NORM_SQ)
                        {
                        mcApp_focParam.Vds = MAX_NORM_SQ;
                        }

                        mcApp_focParam.Vqs = sqrtf(MAX_NORM_SQ-mcApp_focParam.Vds);
                        mcApp_focParam.VqRefVoltage = -mcApp_focParam.MaxPhaseVoltage*mcApp_focParam.Vqs;

                         //Calculating Flux Weakening value of Id, Id_flux_Weakening = (Vqref- Rs*Iq - BEMF)/omega*Ls

                        mcApp_ControlParam.Id_FW_Raw = (mcApp_focParam.VqRefVoltage - (MOTOR_PER_PHASE_RESISTANCE * mcApp_IRef_DQParam.q) 
                                                -(mcApp_ControlParam.VelRef  * MOTOR_BACK_EMF_CONSTANT_Vpeak_PHASE_RAD_PER_SEC_ELEC))/(mcApp_ControlParam.VelRef  * MOTOR_PER_PHASE_INDUCTANCE);

                        mcApp_ControlParam.Id_FW_Filtered = mcApp_ControlParam.Id_FW_Filtered +
                                          ((mcApp_ControlParam.Id_FW_Raw - mcApp_ControlParam.Id_FW_Filtered) * mcApp_ControlParam.qKfilterIdRef) ;										
                        mcApp_IRef_DQParam.d= mcApp_ControlParam.Id_FW_Filtered;						
                          //Limit Id such that MAX_FW_NEGATIVE_ID_REF < Id < 0
                        if(mcApp_IRef_DQParam.d> 0)
                            mcApp_IRef_DQParam.d= 0; 

                        if(mcApp_IRef_DQParam.d< MAX_FW_NEGATIVE_ID_REF)
                            mcApp_IRef_DQParam.d= MAX_FW_NEGATIVE_ID_REF;

                        // Limit Q axis current such that sqrtf(Id^2 +Iq^2) <= MAX_MOTOR_CURRENT
                        mcApp_ControlParam.Iqmax = -sqrtf((MAX_MOTOR_CURRENT_SQUARED) - (mcApp_IRef_DQParam.d*mcApp_IRef_DQParam.d));	
                    }
                    else
                    {
                        mcApp_IRef_DQParam.d= 0;
                        mcApp_ControlParam.Iqmax = -MAX_MOTOR_CURRENT;
                        mcApp_ControlParam.Id_FW_Filtered = 0;
                    }
            }


    #endif 	

    #else
    if(mcApp_motorState.motorDirection == 0)
    {
            mcApp_IRef_DQParam.q= (float)((float)potReading * TORQUE_MODE_POT_ADC_RATIO); // During torque mode, Iq = Potentiometer provides torque reference in terms of current, Id = 0
            mcApp_IRef_DQParam.d= 0;
            mcApp_ControlParam.Iqmax = TORQUE_MODE_MAX_CUR;
    }
    else
    {
            mcApp_IRef_DQParam.q= (float)((float)-potReading * TORQUE_MODE_POT_ADC_RATIO); // During torque mode, Iq = Potentiometer provides torque reference in terms of current, Id = 0
            mcApp_IRef_DQParam.d= 0;
            mcApp_ControlParam.Iqmax = -TORQUE_MODE_MAX_CUR;
    }
    #endif  // endif for TORQUE_MODE
	

        break;
        }
     }
    
#else // CURPI_TUN
mcApp_IRef_DQParam.d= CUR_STEP_AMP;
mcApp_ControlParam.AssertActiveVector = 1;
if(curpi_counter < CPT_CNT_VAL)
{
    curpi_counter++;
}
else
{
    mcApp_motorStop();
    curpi_counter = 0;
}

#endif //CURPI_TUN    

                // PI control for D
        mcApp_D_PIParam.qInMeas = mcApp_I_DQParam.d;          // This is in Amps
        mcApp_D_PIParam.qInRef  = mcApp_IRef_DQParam.d;      // This is in Amps
        mcLib_CalcPI(&mcApp_D_PIParam);
        mcApp_V_DQParam.d    =  mcApp_D_PIParam.qOut;          // This is in %. If should be converted to volts, multiply with DCBus/sqrt(3)

        // dynamic d-q adjustment
        // with d component priority
        // vq=sqrt (vs^2 - vd^2)
        // limit vq maximum to the one resulting from the calculation above
        DoControl_Temp2 = mcApp_D_PIParam.qOut * mcApp_D_PIParam.qOut;
        DoControl_Temp1 = MAX_NORM_SQ - DoControl_Temp2;
        mcApp_Q_PIParam.qOutMax = sqrtf(DoControl_Temp1);
        mcApp_Q_PIParam.qOutMin = -mcApp_Q_PIParam.qOutMax;        
	
            if(mcApp_motorState.motorDirection == 0)
            {
                //Limit Q axis current
                if(mcApp_IRef_DQParam.q>mcApp_ControlParam.Iqmax)
                {
                mcApp_IRef_DQParam.q= mcApp_ControlParam.Iqmax;
                }
                else
                {

                }
            }
            else
            {
                //Limit Q axis current
                if(mcApp_IRef_DQParam.q<mcApp_ControlParam.Iqmax)
                {
                mcApp_IRef_DQParam.q= mcApp_ControlParam.Iqmax;
                }   
                else
                {

                }
            }
        // PI control for Q
        mcApp_Q_PIParam.qInMeas = mcApp_I_DQParam.q;          // This is in Amps
        mcApp_Q_PIParam.qInRef  = mcApp_IRef_DQParam.q;      // This is in Amps
        mcLib_CalcPI(&mcApp_Q_PIParam);
        mcApp_V_DQParam.q    =  mcApp_Q_PIParam.qOut;          // This is in %. If should be converted to volts, multiply with DCBus/sqrt(3)   

        mcLib_SinCosGen(&mcApp_SincosParam);

        mcLib_InvParkTransform(&mcApp_V_DQParam,&mcApp_SincosParam, &mcApp_V_AlphaBetaParam);
          // Update LastValpha and LastVbeta
        mcApp_EstimParam.qLastValpha = (mcApp_focParam.MaxPhaseVoltage * mcApp_V_AlphaBetaParam.alpha);
        mcApp_EstimParam.qLastVbeta = (mcApp_focParam.MaxPhaseVoltage * mcApp_V_AlphaBetaParam.beta);
        mcLib_SVPWMGen(&mcApp_V_AlphaBetaParam , &mcApp_SVGenParam);
    if(mcApp_ControlParam.AssertActiveVector == 1)
    {
        invDutyA =  PWM_PERIOD_COUNT -  mcApp_SVGenParam.dPWM_A;
        invDutyB =  PWM_PERIOD_COUNT -  mcApp_SVGenParam.dPWM_B;
        invDutyC =  PWM_PERIOD_COUNT -  mcApp_SVGenParam.dPWM_C;
        
    /* TCC module when operated in center aligned mode, results in ON time 
    which inversely proportional to Duty Cycle value (CC). 
    Hence, the inverted duty value is used to generate desired PWM signals*/
        
        TCC0_PWM24bitDutySet(TCC0_CHANNEL0,(uint32_t) invDutyA);
        TCC0_PWM24bitDutySet(TCC0_CHANNEL1,(uint32_t) invDutyB);
        TCC0_PWM24bitDutySet(TCC0_CHANNEL2,(uint32_t) invDutyC);
    }
    else
    {
        TCC0_PWM24bitDutySet(TCC0_CHANNEL0,(uint32_t) PWM_HALF_PERIOD_COUNT );
        TCC0_PWM24bitDutySet(TCC0_CHANNEL1,(uint32_t) PWM_HALF_PERIOD_COUNT );
        TCC0_PWM24bitDutySet(TCC0_CHANNEL2,(uint32_t) PWM_HALF_PERIOD_COUNT );
    }
        
}
else
{
        TCC0_PWM24bitDutySet(TCC0_CHANNEL0,(uint32_t) PWM_HALF_PERIOD_COUNT );
        TCC0_PWM24bitDutySet(TCC0_CHANNEL1,(uint32_t) PWM_HALF_PERIOD_COUNT );
        TCC0_PWM24bitDutySet(TCC0_CHANNEL2,(uint32_t) PWM_HALF_PERIOD_COUNT );
    
}
          while(ADC0_REGS->ADC_INTFLAG != ADC_INTFLAG_RESRDY_Msk);
                       
        /* Read the ADC result value */
        mcApp_focParam.DCBusVoltage = ((float)ADC1_ConversionResultGet())* VOLTAGE_ADC_TO_PHY_RATIO; // Reads and translates to actual bus voltage
		potReading = ADC0_ConversionResultGet();
        /* select the next ADC channel for conversion */
        ADC0_ChannelSelect(ADC_POSINPUT_AIN0,ADC_NEGINPUT_GND); // Phase U to ADC0
        ADC1_ChannelSelect(ADC_POSINPUT_AIN0,ADC_NEGINPUT_GND); // Phase V to ADC1
         /* Clear all interrupt flags */
        ADC0_REGS->ADC_INTFLAG = ADC_INTFLAG_Msk;
        ADC0_REGS->ADC_INTENSET = ADC_INTFLAG_RESRDY_Msk;// Enable ADC interrupt
       
      
        mcApp_focParam.MaxPhaseVoltage = (float)(mcApp_focParam.DCBusVoltage*ONE_BY_SQRT3);     
        delay_10ms.count++; 

}


// *****************************************************************************
// *****************************************************************************
// Section: MC PI Controller Routines
// *****************************************************************************
// *****************************************************************************

void mcApp_InitControlParameters(void)
{
	// PI D Term     
    mcApp_D_PIParam.qKp = D_CURRCNTR_PTERM;       
    mcApp_D_PIParam.qKi = D_CURRCNTR_ITERM;              
    mcApp_D_PIParam.qKc = D_CURRCNTR_CTERM;
    mcApp_D_PIParam.qdSum = 0;
    mcApp_D_PIParam.qOutMax = D_CURRCNTR_OUTMAX;
    mcApp_D_PIParam.qOutMin = -mcApp_D_PIParam.qOutMax;

    mcLib_InitPI(&mcApp_D_PIParam);

    // PI Q Term 
    mcApp_Q_PIParam.qKp = Q_CURRCNTR_PTERM;    
    mcApp_Q_PIParam.qKi = Q_CURRCNTR_ITERM;
    mcApp_Q_PIParam.qKc = Q_CURRCNTR_CTERM;
    mcApp_Q_PIParam.qdSum = 0;
    mcApp_Q_PIParam.qOutMax = Q_CURRCNTR_OUTMAX;
    mcApp_Q_PIParam.qOutMin = -mcApp_Q_PIParam.qOutMax;
    if(mcApp_motorState.motorDirection == 0)
    {
        mcApp_ControlParam.Iqmax = MAX_MOTOR_CURRENT;
    }
    else
    {
       mcApp_ControlParam.Iqmax = -MAX_MOTOR_CURRENT; 
    }
    mcLib_InitPI(&mcApp_Q_PIParam);

    // PI Qref Term
    mcApp_Speed_PIParam.qKp = SPEEDCNTR_PTERM;       
    mcApp_Speed_PIParam.qKi = SPEEDCNTR_ITERM;       
    mcApp_Speed_PIParam.qKc = SPEEDCNTR_CTERM;  
    mcApp_Speed_PIParam.qdSum = 0;
    mcApp_Speed_PIParam.qOutMax = SPEEDCNTR_OUTMAX;   
    mcApp_Speed_PIParam.qOutMin = -mcApp_Speed_PIParam.qOutMax;

    mcLib_InitPI(&mcApp_Speed_PIParam);
	
    mcApp_ControlParam.qKfilterIdRef = KFILTER_IDREF;
	
	
	return;
}
// *****************************************************************************
// *****************************************************************************
// Section: MC PLL Estimator Parameter Initialization Routine
// *****************************************************************************
// *****************************************************************************
void mcApp_InitEstimParm(void)  
{
	mcApp_EstimParam.qLsDt = (float)(MOTOR_PER_PHASE_INDUCTANCE/LOOPTIME_SEC);
	mcApp_EstimParam.qRs = MOTOR_PER_PHASE_RESISTANCE;
	mcApp_EstimParam.qKFi = (float)(MOTOR_BACK_EMF_CONSTANT_Vpeak_PHASE_RAD_PER_SEC_ELEC);
	mcApp_EstimParam.qInvKFi= (float)INVKFi_BELOW_BASE_SPEED;
    mcApp_EstimParam.qNominal_Speed = (float)NOMINAL_SPEED_RAD_PER_SEC_ELEC;
    mcApp_EstimParam.qDecimate_Nominal_Speed = (float)((float)NOMINAL_SPEED_RAD_PER_SEC_ELEC/10);
   	mcApp_EstimParam.qOmegaMr=0;
	 
    mcApp_EstimParam.qKfilterEsdq = KFILTER_ESDQ;
    mcApp_EstimParam.qVelEstimFilterK = KFILTER_VELESTIM;

    mcApp_EstimParam.qDeltaT = LOOPTIME_SEC;

}


void mcApp_motorStart()
{
    mcApp_InitControlParameters();
	mcApp_InitEstimParm();
    mcApp_IRef_DQParam.d= 0;
    mcApp_IRef_DQParam.q= 0;
    mcApp_ControlParam.Id_FW_Filtered = 0;
    mcApp_ControlParam.VelInput = 0;
    mcApp_ControlParam.VelRef = 0;
    #ifdef ENABLE_WINDMILLING
    mcApp_motorState.focStateMachine = WINDMILLING;
    #else
    mcApp_motorState.focStateMachine = ALIGN;
    #endif
    mcApp_SincosParam.Angle = 0;
	mcApp_SVGenParam.PWMPeriod = (float)MAX_DUTY;
    Align_Counter = 0;
    mcApp_motorState.focStart = 1;
    TCC0_PWM24bitDutySet(TCC0_CHANNEL0,(uint32_t) PWM_HALF_PERIOD_COUNT );
    TCC0_PWM24bitDutySet(TCC0_CHANNEL1,(uint32_t) PWM_HALF_PERIOD_COUNT );
    TCC0_PWM24bitDutySet(TCC0_CHANNEL2,(uint32_t) PWM_HALF_PERIOD_COUNT );
    PWM_Output_Enable();
}

void mcApp_motorStop()
{
    mcApp_motorState.focStart = 0;
    mcApp_IRef_DQParam.d= 0;
    mcApp_IRef_DQParam.q= 0;
    mcApp_I_DQParam.d = 0;
    mcApp_I_DQParam.q = 0;
    state_count = 0;
    motor_state_machine.align = 0;
    motor_state_machine.closed_loop = 0;
    motor_state_machine.open_loop = 0;
    motor_state_machine.open_loop_to_closed_loop = 0;
    motor_state_machine.passive_brake = 0;
    motor_state_machine.windmilling = 0;
    motor_state_machine.windmilling_decide = 0;
    Passive_Brake_Counter = 0;
    PWM_Output_Disable(); 
}

void mcApp_motorStartToggle()
{
    mcApp_motorState.motorStart = !mcApp_motorState.motorStart;
    if(mcApp_motorState.motorStart == 1)
    {
        mcApp_motorStart();
        LED1_OC_FAULT_Clear();
        
    }
    else
    {
        mcApp_motorStop();
    }
}

void mcApp_motorDirectionToggle()
{
    if(mcApp_motorState.motorStart == 0)
    {
        /*Change Motor Direction Only when Motor is Stationary*/
        mcApp_motorState.motorDirection = !mcApp_motorState.motorDirection;
        LED2_Direction_Toggle();
    }
    else
    {
        mcApp_motorState.motorDirection = mcApp_motorState.motorDirection;
    }
}



void OC_FAULT_ISR(uintptr_t context)
{
    
    mcApp_motorStop();
    ADC0_Disable();
    TCC0_PWMStop();
    LED1_OC_FAULT_Set();
    mcApp_motorState.motorStart = 0;
    mcApp_motorState.focStart = 0;
    overCurrentFaultActive = 1; // Set overCurrentFault Flag
    TCC0_REGS->TCC_CTRLBSET = TCC_CTRLBSET_CMD(TCC_CTRLBSET_CMD_RETRIGGER_Val); // Clear the COUNT value
    delay_10ms.count = 0;  // Reset the 10mS counter
    TCC0_REGS->TCC_STATUS = TCC_STATUS_FAULT0(1); // Clear Non Recoverable Fault
    TCC0_PWMStart();
    ADC0_Enable();
    
}