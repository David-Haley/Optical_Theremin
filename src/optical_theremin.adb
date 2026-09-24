--  This program is used to create the tone generator look up table. It converts
--  the frequency distance measurement to the phase step per sample required
--  for DDS frequency generation.

--  Author    : David Haley
--  Created   : 18/09/2026
--  Last Edit : 25/09/2026

--  20260925 : Revised Sin_2 waveform emulating vaccun tupe second order
--  distortion f(Y) := 0.5 * Y ** 2 + 0.75 * Y - 0.25
--  20260923 : RMS power leveling applied to all wavefotms, sine_2
--  (sin (X)**2 added, emulating vaccum tube distortion.

with Ada.Text_IO; use Ada.Text_IO;
with Ada.Calendar; use Ada.Calendar;
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
   "Optical_Theremin configuration tool version 20260925";
   Author : constant String := "Author : David Haley";
   Documentation : constant String := "../Documents/";
   Source : constant String := "../src/";

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
   type Sample_Arrays is array (Sample_Indices) of Samples;
   type Wave_Tables is array (Waveforms) of Sample_Arrays;

   package Sample_IO is new Ada.Text_IO.Integer_IO (Samples);
   use Sample_IO;

   type Properties is record
      DC_Offset, RMS : Reals;
      Minimum, Maximum : Samples;
   end record; -- Properties
   type Property_Tables is array (Waveforms) of Properties;

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

      --  Power leveling is applied such that all waveforms have the same RMS
      --  power. For the same peak amplitude the Sine_2 waveform has the least
      --  power and the other waveforms are normalised to match this.
      --  The relative power of each waveform is as follows:
      --  Sine     : 1.0 /Sqrt (2.0)
      --  Sine_2   : Sqrt (5.0) / 4.0
      --  Triangle : 1.0 / Sqrt (3.0)
      --  Square   : 1.0

      Sine_Amp : constant Reals := 
        Reals (Samples'Last) * (Sqrt (5.0) / 4.0) / (1.0 /Sqrt (2.0));
      Sine_2_Amp : constant Reals := Reals (Samples'Last);
      Triangle_Amp : constant Reals :=
        Reals (Samples'Last) * (Sqrt (5.0) / 4.0) / (1.0 / Sqrt (3.0));
      Square_Amp : constant Samples :=
        Samples (Reals'Rounding (Reals (Samples'Last) * (Sqrt (5.0) / 4.0)));

      Pi_4 : constant Sample_Indices := Sample_Count / 4;
      Pi_2 : constant Sample_Indices := Sample_Count / 2;
      Pi_3_4 : constant Sample_Indices := (Sample_Count * 3) / 4;
      Slope : constant Reals := Triangle_Amp / Reals (Pi_4);

   begin -- Build
      for S in Sample_Indices loop
         Wave_Table (Sine) (S) :=
           Samples (Reals'Rounding (Sine_Amp *
           Sin (Reals (S) / Reals (Sample_Count) * 2.0 * Pi)));
         Wave_Table (Sine_2) (S) :=
           Samples (Reals'Rounding (Sine_2_Amp *
           (0.5 * Sin (Reals (S) / Reals (Sample_Count) * 2.0 * Pi) ** 2 +
            0.75 * Sin (Reals (S) / Reals (Sample_Count) * 2.0 * Pi) - 0.25)));
         if S < Pi_4 then
            Wave_Table (Triangle) (S) :=
              Samples (Reals'Rounding (Reals (S) * Slope));
         elsif S < Pi_3_4 then
            Wave_Table (Triangle) (S) :=
              Samples (Reals'Rounding (Triangle_Amp -
                                       Reals (S - Pi_4) * Slope));
         else
            Wave_Table (Triangle) (S) :=
              Samples (Reals'Rounding (Reals (S - Pi_3_4) * Slope -
                                       Triangle_Amp));
         end if; -- S < Pi_4
         if S < Pi_2 then
            Wave_Table (Square) (S) := Square_Amp;
         else
            Wave_Table (Square) (S) := -Square_Amp;
         end if; -- S < Pi_2
      end loop; -- S in Sample_Indices
   end Build;

   procedure Put (Wave_Table : in Wave_Tables) is

      Output_File : File_Type;

   begin -- Put
      Create (Output_File, Out_File, Documentation & "Sample.csv");
      Put_Line (Output_File, """Phase"",""Sine"",""Sine_2"",""Triangle""," &
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

   procedure Wave_Properties (Wave_Table : in Wave_Tables;
                              Property_Table : out Property_Tables) is

      function Average (Sample_Array : in Sample_Arrays) return Reals is

         Result : Reals := 0.0;

      begin -- Average
         for S in Sample_Indices loop
            Result := @ + Reals (Sample_Array (S));
         end loop; -- S in Sample_Indices
         return Result / Reals (Sample_Count);
      end Average;

      function RMS (Sample_Array : in Sample_Arrays) return Reals is

         Result : Reals := 0.0;

      begin -- RMS
         for S in Sample_Indices loop
            Result := @ + Reals (Sample_Array (S)) ** 2;
         end loop; -- S in Sample_Indices
         return Sqrt (Result / Reals (Sample_Count));
      end RMS;

      function Maximum (Sample_Array : in Sample_Arrays) return Samples is

         Result : Samples := Samples'First;

      begin -- Maximum
         for S in Sample_Indices loop
            if Result < Sample_Array (S) then
               Result := Sample_Array (S);
            end if; -- Result < Sample_Array (S)
         end loop; -- S in Sample_Indices
         return Result;
      end Maximum;

      function Minimum (Sample_Array : in Sample_Arrays) return Samples is

         Result : Samples := Samples'Last;

      begin -- Minimum
         for S in Sample_Indices loop
            if Result > Sample_Array (S) then
               Result := Sample_Array (S);
            end if; -- Result > Sample_Array (S)
         end loop; -- S in Sample_Indices
         return Result;
      end Minimum;

   begin -- Wave_Properties
      for W in Waveforms loop
         Property_Table (W).DC_Offset := Average (Wave_Table (W));
         Property_Table (W).RMS := RMS (Wave_Table (W));
         Property_Table (W).Maximum := Maximum (Wave_Table (W));
         Property_Table (W).Minimum := Minimum (Wave_Table (W));
      end loop; -- W in Waveforms
   end Wave_Properties;

   procedure Put (Property_Table : in Property_Tables) is

   begin -- Put
      Put_Line ("Check of waveform properties:");
      Put_Line ("DC Offset should be close to 0.0");
      Put_Line ("The RMS values for all waveforms should be similar");
      New_Line;
      Put_Line ("Waveform      Sine   Sine_2 Triangle   Square");
      Put ("DC Offset");
      for W in Waveforms loop
         Put (Property_Table (W).DC_Offset, 3, 5, 0);
      end loop; -- W in Waveforms
      New_Line;
      Put ("RMS      ");
      for W in Waveforms loop
         Put (Property_Table (W).RMS, 7, 1, 0);
      end loop; -- W in Waveforms
      New_Line;
      Put ("Maximum  ");
      for W in Waveforms loop
         Put (Property_Table (W).Maximum, 9);
      end loop; -- W in Waveforms
      New_Line;
      Put ("Minimum  ");
      for W in Waveforms loop
         Put (Property_Table (W).Minimum, 9);
      end loop; -- W in Waveforms
      New_Line;
   end Put;

   procedure Write_Header (Wave_Table : in Wave_Tables) is

      File_Name : constant String := "dds_generator.h";
      Output_File : File_Type;
      Indent_Columns : constant Positive_Count := 3;

      procedure Comment (Text : in String;
                        Indent : in Natural := 0) is

      begin -- Comment
         if Col (Output_File) = Positive_Count'First then
            if Indent > 0 then
               Set_Col (Output_File, Positive_Count (Indent) * Indent_Columns);
            end if; --
            Put (Output_File, "// ");
         else
            Put (Output_File, " // ");
         end if; -- Col (Output_File) = Positive_Count'First
         Put_Line (Output_File, Text);
      end Comment;

      procedure Indent (Indents : in Positive_Count := 1) is

      begin -- Indent
         Set_Col (Output_File, Indents * Indent_Columns);
      end Indent;

   begin -- Write_Header
      Put_Line ("Writing " & Source & File_Name);
      Create (Output_File, Out_File, Source & File_Name);
      Comment ("This header file contains the waveform tables and other");
      Comment ("declaratopns for the DDS waveform generator.");
      Comment ("Generated by : " & Name_and_Version);
      Comment ("Run date and time : " & Local_Image (Clock));
      Comment (Author);
      New_Line (Output_File);
      Close (Output_File);
   end Write_Header;

   Frequency_Table : Frequency_Tables;
   Wave_Table : Wave_Tables;
   Property_Table : Property_Tables;

begin -- Optical_Theremin 
   Put_Line (Name_and_Version);
   Build (Frequency_Table);
   Put (Frequency_Table);
   Build (Wave_Table);
   Put (Wave_Table);
   Wave_Properties (Wave_Table, Property_Table);
   Put (Property_Table);
   Write_Header (Wave_Table);
end Optical_Theremin;