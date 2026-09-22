--  This program is used to create the tone generator look up table. It converts
--  the frequency distance measurement to the phase step per sample required
--  for DDS frequency generation.

--  Author    : David Haley
--  Created   : 18/09/2026
--  Last Edit : 23/09/2026

--  20260923 : RMS power leveling applied to all wavefotms, sine_2
--  (sin (X)**2 added, emulating vaccum tube distortion.

with Ada.Text_IO; use Ada.Text_IO;
with Ada.Calendar.Formatting; use Ada.Calendar.Formatting;
with Ada.Numerics; use Ada.Numerics;
with Ada.Numerics.Generic_Elementary_Functions;
with Interfaces; use Interfaces;

procedure Optical_Theremin is

   type Reals is digits 15;

   package Real_Numerics is new
     Ada.Numerics.Generic_Elementary_Functions (Reals);
   use Real_Numerics;

   package Real_IO is new Ada.Text_IO.Float_IO (Reals);
   use Real_IO;
   
   subtype Angles is Unsigned_32;

   Name_and_Version : constant String :=
   "Optical_Theremin configuration tool version 20260923";
   Author : constant String := "Author : David Haley";
   Documentation : constant String := "../Documents/";

   Sample_Rate : constant Reals := 44100.0; -- 44.1 khz
   Step_Per_Hz : constant Reals := (Reals (Angles'Last) + 1.0) / Sample_Rate;
   A0 : constant Reals := 27.5;
   Octaves : constant Positive := 8;
   MM_per_Octave : constant Positive := 40; -- distance in mm to double frequency
   Higest_Note_MM : constant Positive := 60; -- distance for highest note in mm
   Lowest_Note_MM : constant Positive :=
     Higest_Note_MM + MM_per_Octave * Octaves;

   subtype Frequency_Indices is Positive range Higest_Note_MM .. Lowest_Note_MM;
   type Frequency_Element is record
      Frequency : Reals;
      Step : Unsigned_32;
   end record; -- Frequency_Element
   type Frequency_Tables is array (Frequency_Indices) of Frequency_Element;
   
   subtype Samples is Integer_16 range -Integer_16'Last .. Integer_16'Last;
   Sample_Count : constant Unsigned_16 := 1024; -- Steps per cycle
   subtype Sample_Indices is Unsigned_16 range 0 .. Sample_Count - 1;
   type Waveforms is (Sine, Sine_2, Triangle, Square);
   type Sample_Array is array (Sample_Indices) of Samples;
   type Wave_Tables is array (Waveforms) of Sample_Array;

   procedure Build (Frequency_Table : out Frequency_Tables) is

   begin -- Build_Frequency_Table
      for F in Frequency_Indices loop
         Frequency_Table (F).Frequency :=
         (Reals (2.0) ** (Reals (Lowest_Note_MM - F) / Reals (MM_per_Octave))
         * A0);
         Frequency_Table (F).Step :=
           Angles (Reals'Rounding (Frequency_Table (F).Frequency
           * Step_Per_Hz));
      end loop; -- F in Frequency_Indices
   end Build;

   procedure Put (Frequency_Table : in Frequency_Tables) is

      Output_File : File_Type;

   begin -- Put
      Create (Output_File, Out_File, Documentation & "Frequency.csv");
      Put_Line (Output_File, """Distance"",""Frequency"",""Step""");
      for F in Frequency_Indices loop
         Put (Output_File, F'Img & ",");
         Put (Output_File, Frequency_Table (F).Frequency, 5, 3, 0);
         Put_Line (Output_File, "," & Frequency_Table (F).Step'Img);
      end loop; -- F in Frequency_Indices
      Close (Output_File);
   end Put;

   procedure Build (Wave_Table : out Wave_Tables) is

      Pi_4 : constant Sample_Indices := Sample_Count / 4;
      Pi_2 : constant Sample_Indices := Sample_Count / 2;
      Pi_3_4 : constant Sample_Indices := (Sample_Count * 3) / 4;
      Slope : constant Reals := Reals (Samples'Last) / Reals (Pi_4);
      -- Sine_Amp (amplitude) is calculated to equal the RMS power of the
      -- triangle waveform. 1 / Sqrt (2.0)
      Sine_Amp : constant Reals := 
        Reals (Samples'Last) * Sqrt (2.0) / Sqrt (3.0);
      -- Sine_Amp (amplitude) is calculated to equal the RMS power of the
      -- triangle waveform. Sqrt (6.0) / 4.0
      Sine_2_Amp : constant Reals :=
        Reals (Samples'Last) * 4.0 / Sqrt (18.0);
      -- Square_Amp (amplitude) is calculated to equal the RMS power of the
      -- triangle waveform. (Triangle : 1 / Sqrt (3.0))
      Square_Amp : constant Samples :=
        Samples (Reals'Rounding (Reals (Samples'Last) / Sqrt (3.0)));

   begin -- Build
      for S in Sample_Indices loop
         Wave_Table (Sine) (S) :=
           Samples (Reals'Rounding (Sine_Amp *
           Sin (Reals (S) / Reals (Sample_Count) * 2.0 * Pi)));
         if S < Pi_4 then
            Wave_Table (Triangle) (S) :=
              Samples (Reals'Rounding (Reals (S) * Slope));
         elsif S < Pi_3_4 then
            Wave_Table (Triangle) (S) :=
              Samples (Reals'Rounding (Reals (Samples'Last) -
                                       Reals (S - Pi_4) * Slope));
         else
            Wave_Table (Triangle) (S) :=
              Samples (Reals'Rounding (Reals (Samples'First) +
                                       Reals (S - Pi_3_4) * Slope));
         end if; -- S < Pi_4
         if S < Pi_2 then
            Wave_Table (Sine_2) (S) :=
              Samples (Reals'Rounding (Sine_2_Amp *
              (Sin (Reals (S) / Reals (Sample_Count) * 2.0 * Pi) ** 2)));
            Wave_Table (Square) (S) := Square_Amp;
         else
            Wave_Table (Sine_2) (S) :=
              Samples (Reals'Rounding (-Sine_2_Amp *
              (Sin (Reals (S) / Reals (Sample_Count) * 2.0 * Pi) ** 2)));
            Wave_Table (Square) (S) := -Square_Amp;
         end if; -- S < Pi_2
      end loop; -- S in Sample_Indices
   end Build;

   procedure Put (Wave_Table : in Wave_Tables) is

      Output_File : File_Type;

   begin -- Put
      Create (Output_File, Out_File, Documentation & "Sample.csv");
      Put_Line (Output_File, """Phase"",""Sine"",""Sine ** 2"",""Triangle""," &
                """Square""");
      for S in Sample_Indices loop
         Put_Line (Output_File, S'Img & "," &
                   Samples'Image (Wave_Table (Sine) (S)) & "," &
                   Samples'Image (Wave_Table (Sine_2) (S)) & "," &
                   Samples'Image (Wave_Table (Triangle) (S)) & "," &
                   Samples'Image (Wave_Table (Square) (S)));
      end loop; -- S in Sample_Indices
      Close (Output_File);
   end Put;

   Frequency_Table : Frequency_Tables;
   Wave_Table : Wave_Tables;

begin -- Optical_Theremin 
   Put_Line (Name_and_Version);
   Build (Frequency_Table);
   Put (Frequency_Table);
   Build (Wave_Table);
   Put (Wave_Table);
end Optical_Theremin;