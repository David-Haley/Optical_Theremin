--  This program is used to create the tone generator look up table. It converts
--  the frequency distance measurement to the phase step per sample required
--  for DDS frequency generation.

--  Author    : David Haley
--  Created   : 18/09/2026
--  Last Edit : 18/09/2026

with Ada.Text_IO; use Ada.Text_IO;
with Ada.Calendar.Formatting; use Ada.Calendar.Formatting;
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
   "Optical_Theremin configuration tool version 20260918";
   Author : constant String := "Author : David Haley";
   Documentation : constant String := "../Documents/";

   Sample_Rate : constant Reals := 44100.0; -- 44.1 khz
   Steps_Per_Cycle : constant Positive := 1024;
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

   Frequency_Table : Frequency_Tables;

begin -- Optical_Theremin 
   Put_Line (Name_and_Version);
   Build (Frequency_Table);
   Put (Frequency_Table);
end Optical_Theremin;