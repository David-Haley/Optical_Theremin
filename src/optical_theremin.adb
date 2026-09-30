--  This program is used to create the tone generator look up table. It converts
--  the frequency distance measurement to the phase step per sample required
--  for DDS frequency generation.

--  Author    : David Haley
--  Created   : 18/09/2026
--  Last Edit : 29/09/2026

--  20260926 : Reduced range to five octaves. Define for Phase_Shift added
--  to DDS_generator header file.
--  20260926 : Header file generation added.
--  20260925 : Revised Sin_2 waveform emulating vaccun tupe second order
--  distortion f(Y) := 0.5 * Y ** 2 + 0.75 * Y - 0.25
--  20260923 : RMS power leveling applied to all wavefotms, sine_2
--  (sin (X)**2 added, emulating vaccum tube distortion.

with Ada.Strings; use Ada.Strings;
with Ada.Strings.Fixed; use Ada.Strings.Fixed;
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
   "Optical_Theremin configuration tool version 20260929";
   Author : constant String := "Author : David Haley";
   Documentation : constant String := "../Documents/";
   Source : constant String := "../src/";

   Sample_Rate : constant Reals := 44100.0; -- 44.1 khz;
   Step_Per_Hz : constant Reals := (Reals (Angles'Last) + 1.0) / Sample_Rate;
   A0 : constant Reals := 27.5;
   A1 : constant Reals := A0 * 2.0;
   A2 : constant Reals := A1 * 2.0;
   Octaves : constant Positive := 5;
   MM_per_Octave : constant Positive := 60; -- distance in mm to double frequency
   Highest_Note_MM : constant Positive := 60; -- distance for highest note in mm
   Lowest_Note_MM : constant Positive :=
     Highest_Note_MM + MM_per_Octave * Octaves;

   subtype Frequency_Indices is Positive range Highest_Note_MM .. Lowest_Note_MM;
   type Frequency_Element is record
      Frequency : Reals;
      Step : Unsigned_32;
   end record; -- Frequency_Element
   type Frequency_Tables is array (Frequency_Indices) of Frequency_Element;

   package Step_IO is new Ada.Text_IO.Modular_IO (Unsigned_32);
   use Step_IO;
   
   subtype Samples is Integer_16 range -Integer_16'Last .. Integer_16'Last;
   Sample_Count : constant Unsigned_16 := 1024;
   --  Steps per cycle, must be a power of two to allow to allow the table
   --  Wave_Table index to be calculated by a by a right shift rather than a
   --  devision. The constant Phase_Shift must be defined such that shifting an
   --  Unsigned_32 to the right Power_Shift times results in a modumo
   --  Sample_Count number.
   Phase_Shift : constant Positive := 22;
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
         * A1);
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

   procedure Write_Headers (Frequency_Table : in Frequency_Tables;
                            Wave_Table : in Wave_Tables) is

      DDS_File_Name : constant String := "dds_generator.h";
      DDS_Table_File_Name : constant String := "dds_table.h";
      Output_File : File_Type;
      Indent_Columns : constant Positive_Count := 3;
      Waveform_Count : constant Positive :=
        Waveforms'Pos (Waveforms'Last) - Waveforms'Pos (Waveforms'First) + 1;

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

   begin -- Write_Headers
      Put_Line ("Writing " & Source & DDS_File_Name);
      Create (Output_File, Out_File, Source & DDS_File_Name);
      Comment ("This header file contains declarations for the");
      Comment ("DDS waveform generator.");
      Comment ("Generated by : " & Name_and_Version);
      Comment ("Run date and time : " & Local_Image (Clock));
      Comment (Author);
      New_Line (Output_File);
      Put_Line (Output_File, "#ifndef DDS_GENERATOR_H");
      Put_Line (Output_File, "#define DDS_GENERATOR_H");
      New_Line (Output_File);
      Put_Line (Output_File, "#include <stdint.h>");
      New_Line (Output_File);
      Put (Output_File, "#define Sample_Rate" & Positive (Sample_Rate)'Img);
      Comment ("Sample rate in Hz");
      Put_Line (Output_File, "#define Highest_Note_MM" & Highest_Note_MM'Img);
      Put_Line (Output_File, "#define Lowest_Note_MM" & Lowest_Note_MM'Img);
      New_Line (Output_File);
      Put_Line (Output_File, "typedef enum {");
      Indent;
      Put_Line (Output_File, "Sine     = 0,");
      Indent;
      Put_Line (Output_File, "Sine_2   = 1,");
      Indent;
      Put_Line (Output_File, "Triangle = 2,");
      Indent;
      Put_Line (Output_File, "Square   = 3");
      Put_Line (Output_File, "} Waveforms;");
      New_Line (Output_File);
      Put_Line (Output_File, "typedef struct {");
      Indent;
      Put_Line (Output_File, "int16_t Left;");
      Indent;
      Put_Line (Output_File, "int16_t Right;");
      Put_Line (Output_File, "} Audio_Sample;");
      New_Line (Output_File);
      Comment ("This function returns a single sample of audio each time it");
      Comment ("is called. The sample rate must be" & Positive (Sample_Rate)'Img
               & "Hz. Waveform");
      Comment ("specifies the type of waveform to be produced and Distance");
      Comment ("the frequency. The maximum frequency is produced at");
      Comment ("Highest_Note_MM and the minimum frequency is produced at");
      Comment ("Lowest_Note_MM. If the measured distance is out of range,");
      Comment ("or more than Lowest_Note_MM, set Distance to");
      Comment ("Lowest_Note_MM. If the measured distance is less than");
      Comment ("Highest_Note_MM then set Distance to Highest_Note_MM.");
      Put_Line (Output_File, "Audio_Sample DDS_Generator (");
      Indent;
      Put_Line (Output_File, "const Waveforms Waveform,");
      Indent;
      Put_Line (Output_File, "const int Distance");
      Put_Line (Output_File, ");");
      New_Line (Output_File);
      Put_Line (Output_File, "#endif // DDS_GENERATOR_H");
      Close (Output_File);
      Put_Line ("Writing " & Source & DDS_Table_File_Name);
      Create (Output_File, Out_File, Source & DDS_Table_File_Name);
      Comment ("This header file contains definitions for the Phase_Step");
      Comment ("and Wave_Table arrays. It is only intended to be included");
      Comment ("in dds_generator.c. The tables are placed in RAM to avoid");
      Comment ("flash (XIP cache miss) latency when generating samples.");
      Comment ("Generated by : " & Name_and_Version);
      Comment ("Run date and time : " & Local_Image (Clock));
      Comment (Author);
      New_Line (Output_File);
      Put_Line (Output_File, "#ifndef DDS_TABLE_H");
      Put_Line (Output_File, "#define DDS_TABLE_H");
      New_Line (Output_File);
      Put_Line (Output_File, "#include <stdint.h>");
      Put_Line (Output_File, "#include ""pico.h""");
      New_Line (Output_File);
      Put_Line (Output_File, "#define Frequency_Count" &
                Positive'Image (Frequency_Indices'Last -
                                Frequency_Indices'First + 1));
      New_Line (Output_File);
      Comment ("The table represents the phase step for each frequency.");
      Comment ("To mitigate the accumulation of errors the phase step is");
      Comment ("treated as a fixed point binary number. The accumulated,");
      Comment ("phase requires a >> 22 applied to reduce it to the");
      Comment ("required range of the Wave_Table index. The first element");
      Comment ("of the array (index 0) corresponds to Highest_Note_MM");
      Comment ("and the last element corresponds to Lowest_Note_MM.");
      Put (Output_File,
           "static const uint32_t __not_in_flash (""dds"") Phase_Step" &
           " [Frequency_Count] = {");
      for F in Frequency_Indices loop
         if F mod 5 = 0 then
            New_Line (Output_File);
            Indent;
         end if; -- F mod 5 = 0
         Put (Output_File, Frequency_Table (F).Step, 10);
         if F /= Frequency_Indices'Last then
            Put (Output_File, ',');
         else
            New_Line (Output_File);
            Put (Output_File, "};");
            Comment ("Phase_Step");
         end if; -- F /= Frequency_Indices'Last
      end loop; -- F in Frequency_Indices
      New_Line (Output_File);
      Put_Line (Output_File, "#define Sample_Count" & Sample_Count'Img );
      Comment ("Length of the Wave_Table for each waveform.");
      New_Line (Output_File);
      Comment ("Number of bits the phase accumulator is shifted right to");
      Comment ("produce a Wave_Table index, 32 - log2 (Wave_Table length).");
      Put_Line (Output_File, "#define Phase_Shift" & Phase_Shift'Img);
      New_Line (Output_File);
      Comment ("The table represents the instantaneous value for each");
      Comment ("waveform for each value of phase.");
      Put_Line (Output_File,
                "static const int16_t __not_in_flash (""dds"") Wave_Table [" &
                Trim (Waveform_Count'Img, Both) & "] [Sample_Count] = {");
      for W in Waveforms loop
         Indent;
         Put (Output_File, '{'); 
         Comment (W'Img);
         for S in Sample_Indices loop
            if S mod 8 = 0 then
               if S = Sample_Indices'First then
                  Indent (2);
               else
                  New_Line (Output_File);
                  Indent (2);
               end if; -- S = Sample_Indices'First
            end if; -- S mod 8 = 0
            Put (Output_File, Wave_Table (W) (S), 7);
            if S /= Sample_Indices'Last then
               Put (Output_File, ',');
            else
               New_Line (Output_File);
               Indent;
               if W = Waveforms'Last then
                  Put (Output_File, '}');
               else
                  Put (Output_File, "},");
               end if; -- W = Waveforms'Last 
               Comment (W'Img);
            end if; -- S /= Sample_Indices'Last
         end loop; -- S in Sample_Indices
      end loop; -- W in Waveforms
      Put (Output_File, "};");
      Comment ("Wave_Table");
      New_Line (Output_File);
      Put_Line (Output_File, "#endif // DDS_TABLE_H");
   end Write_Headers;

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
   Write_Headers (Frequency_Table, Wave_Table);
end Optical_Theremin;