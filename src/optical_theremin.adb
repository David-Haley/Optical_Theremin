--  This program is used to create the tone generator look up table. It converts
--  the frequency distance measurement to the phase step per sample required
--  for DDS frequency generation.

--  Author    : David Haley
--  Created   : 18/09/2026
--  Last Edit : 03/10/2026

--  20261004 : Default_Octave_Range constant and Octave_Range_Count added.
--  20261003 : Tone_Distance changed to Pitch_Distance.
--  20261002 : Octave range selection. The DDS generator shifts the phase step
--  by Octave_Shift, Lowest_Octave_Shift .. Highest_Octave_Shift, so the five
--  octave span can start from A0, A1 or A2. The limits and the octave number
--  of the lowest note are written to the dds generator header, and the
--  largest shifted phase step is checked to be less than 2 ** 31.
--  20261002 : Use an integer divisor for the PIO clock giving a jitter free
--  but nonstandard sample frequency sample_Frequency a define in the dds
--  generator header. The dds is now going to implement volume control so a
--  volume table is now required, it contains a multiplier. To maximise sample
--  accuracy the minimum volume is 2 ** (-17) or approximately -102 dB, thus
--  retaining 14 bits of precision at maximum attenuation.
--  20260926 : Reduced range to five octaves. Define for Phase_Shift added
--  to DDS_generator header file.
--  20260926 : Header file generation added.
--  20260925 : Revised Sine_2 waveform emulating vacuum tube second order
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
   "Optical_Theremin configuration tool version 20261004";
   Author : constant String := "Author : David Haley";
   Documentation : constant String := "../Documents/";
   Source : constant String := "../src/";

   RPi_Pico_Clock : constant Reals := 125.0E06;
   Sample_Bits : constant Positive := 32;
   PIO_Clock_Divisor : constant Positive := 22;
   -- There are two Samples per LRCK and two PIO clock cycles per bit thus an
   -- additional factor of 4 applies.
   Sample_Rate : constant Reals := RPi_Pico_Clock /
     Reals (Sample_Bits * PIO_Clock_Divisor * 4);
   Step_Per_Hz : constant Reals := (Reals (Angles'Last) + 1.0) / Sample_Rate;
   A0 : constant Reals := 27.5;
   Octaves : constant Positive := 5; -- Playable in one octave range
   Full_Octaves : constant Positive := 7; -- Range of Frequency_Table
   MM_per_Octave : constant Positive := 60; -- distance in mm to double frequency
   Highest_Note_MM : constant Positive := 60; -- distance for highest note in mm
   Lowest_Note_MM : constant Positive :=
     Highest_Note_MM + MM_per_Octave * Octaves;

   subtype Frequency_Indices is Natural range 0 .. MM_per_Octave * Full_Octaves;
   type Frequency_Element is record
      Frequency : Reals;
      Step : Unsigned_32;
   end record; -- Frequency_Element
   type Frequency_Tables is array (Frequency_Indices) of Frequency_Element;

   type Octave_Ranges is (A2_A7, G2_G7, F2_F7, E2_E7, D2_D7, C2_C7, B1_B6,
                          A1_A6, G1_G6, F1_F6, E1_E6, D1_D6, C1_C6, B0_B5,
                          A0_A5);
   for Octave_Ranges use (A2_A7 => 0,
                          G2_G7 => 10,
                          F2_F7 => 20,
                          E2_E7 => 25,
                          D2_D7 => 35,
                          C2_C7 => 45,
                          B1_B6 => 50,
                          A1_A6 => MM_per_Octave,
                          G1_G6 => MM_per_Octave + 10,
                          F1_F6 => MM_per_Octave + 20,
                          E1_E6 => MM_per_Octave + 25,
                          D1_D6 => MM_per_Octave + 35,
                          C1_C6 => MM_per_Octave + 45,
                          B0_B5 => MM_per_Octave + 50,
                          A0_A5 => 2 * MM_per_Octave);
   Default_Octave_Range : constant Octave_Ranges := A1_A6;

   package Step_IO is new Ada.Text_IO.Modular_IO (Unsigned_32);
   use Step_IO;
 
   subtype Samples is Integer_32 range -Integer_32'Last .. Integer_32'Last;
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

   Loud_MM : constant Positive := 100;  --  distance for full volume in mm
   Muted_MM : constant Positive := Loud_MM + Positive (Unsigned_8'Last);
   subtype Volume_Indices is Positive range Loud_MM .. Muted_MM; 
   subtype Volumes is Integer_32;
   type Volume_Tables is array (Volume_Indices) of Volumes;

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
         (Reals (2.0) ** (Reals (Frequency_Indices'Last - F) /
           Reals (MM_per_Octave))) * A0;
         Frequency_Table (F).Step :=
           Angles (Reals'Rounding (Frequency_Table (F).Frequency
           * Step_Per_Hz));
      end loop; -- F in Frequency_Indices
   end Build;

   procedure Put (Frequency_Table : in Frequency_Tables) is

      function In_Range (F : in Frequency_Indices;
                         Octave_Range : in Octave_Ranges) return Boolean is

         Start_MM : constant Frequency_Indices := Octave_Ranges'Enum_Rep (Octave_Range);

      begin -- Range
         return Start_MM <= F and F <= Start_MM + Octaves * MM_per_Octave;
      end In_Range;

      Output_File : File_Type;

   begin -- Put
      Create (Output_File, Out_File, Documentation & "Frequency.csv");
      Put (Output_File, """Index"",");
      for R in reverse Octave_Ranges loop
         Put (Output_File, """" & R'Img & """,");
      end loop; -- R reverse in Octave_Ranges loop
      Put_Line (Output_File, """Frequency"",""Step""");
      for F in Frequency_Indices loop
         Put (Output_File, F'Img & ",");
         for R in reverse Octave_Ranges loop
            if In_Range (F, R) then
               Put (Output_File, Positive'Image (Highest_Note_MM + F -
                  Octave_Ranges'Enum_Rep (R)));
            end if; -- Octave_Ranges'Enum_Rep (A0_A5) <= F and
            Put (Output_File, ',');
         end loop; -- R in reverse Octave_Ranges
         Put (Output_File, Frequency_Table (F).Frequency, 4, 5, 0);
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
      Put_Line ("Waveform           Sine        Sine_2      Triangle" &
                "        Square");
      Put ("DC Offset");
      for W in Waveforms loop
         Put (Property_Table (W).DC_Offset, 8, 5, 0);
      end loop; -- W in Waveforms
      New_Line;
      Put ("RMS      ");
      for W in Waveforms loop
         Put ((Property_Table (W).RMS / Property_Table (Sine_2).RMS), 8, 5, 0);
      end loop; -- W in Waveforms
      New_Line;
      Put ("Maximum  ");
      for W in Waveforms loop
         Put (Property_Table (W).Maximum, 14);
      end loop; -- W in Waveforms
      New_Line;
      Put ("Minimum  ");
      for W in Waveforms loop
         Put (Property_Table (W).Minimum,14);
      end loop; -- W in Waveforms
      New_Line;
   end Put;

   procedure Build (Volume_Table : out Volume_Tables) is

      Maximum_Attenuation : constant Reals := -17.0;
      -- 2 ** (-17) Approximately -102 dB

   begin -- Build
      Volume_Table (Volume_Indices'Last) := 0;
      for V in Volume_Indices range
        Volume_Indices'First .. Volume_Indices'Last - 1
      loop
         Volume_Table (V) :=
            Volumes (Reals'Rounding (Reals (Samples'Last) *
            (2.0 ** (Maximum_Attenuation * Reals (V - Loud_MM) /
            Reals (Muted_MM - Loud_MM - 1)))));
      end loop; -- V in Volume_Indices
   end Build;

   procedure Put (Volume_Table : in Volume_Tables) is

      function Db (Volume : in Volumes) return Reals is
         (20.0 * Log (Reals (Volume) / Reals (Samples'Last), 10.0));

      function Db (Previous, Current : in Volumes) return Reals is
         (20.0 * Log (Reals (Current) / Reals (Previous), 10.0));

      Output_File : File_Type;
      Previous : Volumes := Samples'Last;

   begin -- Put
      Create (Output_File, Out_File, Documentation & "Volume.csv");
      Put_Line (Output_File, """Distance"",""Multiplier"",""Step""," &
                """Volume""");
      for V in Volume_Indices loop
         Put (Output_File, V'Img & "," & Volume_Table (V)'Img & ",");
         if Volume_Table (V) = 0 then
            Put (Output_File, """-infinity"",""-infinity""");
         else
            Put (Output_File, Db (Previous, Volume_Table (V)), 2, 2, 0);
            Put (Output_File, ",");
            Put (Output_File, Db (Volume_Table (V)), 4, 2, 0);
         end if; -- Volume_Table (A) = 0
         New_Line (Output_File);
         Previous := Volume_Table (V);
      end loop; -- V in Volume_Indices
      Close (Output_File);
   end Put; 

   procedure Write_Headers (Frequency_Table : in Frequency_Tables;
                            Wave_Table : in Wave_Tables;
                            Volume_Table : in Volume_Tables) is

      DDS_File_Name : constant String := "dds_generator.h";
      DDS_Table_File_Name : constant String := "dds_table.h";
      Output_File : File_Type;
      Indent_Columns : constant Positive_Count := 3;
      Waveform_Count : constant Positive :=
        Waveforms'Pos (Waveforms'Last) - Waveforms'Pos (Waveforms'First) + 1;
      Width_32 : constant Field := 12; -- field width for 32bit integer;

      Frequency_Count_String : constant String := "Frequency_Count";
      Phase_Step_String : constant String := "Phase_Step";
      Waveform_Count_String : constant String := "Waveform_Count";
      Sample_Count_String : constant String := "Sample_Count";
      Wave_Table_String : constant String := "Wave_Table";
      Volume_Count_String : constant String := "Volume_Count";
      Volume_Table_String : constant String := "Volume_Table";

      procedure Comment (Text : in String;
                        Indent : in Natural := 0) is

      begin -- Comment
         if Col (Output_File) = Positive_Count'First then
            if Indent > 0 then
               Set_Col (Output_File, Positive_Count (Indent) * Indent_Columns);
            end if; -- Indent > 0
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
      Put (Output_File, "#define PIO_Clock_Divisor" & PIO_Clock_Divisor'Img);
      Comment ("PIO divisor to set bit rate");
      Put (Output_File, "#define Sample_Rate" & Sample_Rate'Img);
      Comment ("Sample rate in Hz");
      Put (Output_File, "#define Sample_Size" & Sample_Bits'Img);
      Comment ("Sample size in bits");
      New_Line (Output_File);
      Put (Output_File, "#define Highest_Note_MM" & Highest_Note_MM'Img);
      Comment ("Distance for highest frequency");
      Put (Output_File, "#define Lowest_Note_MM" & Lowest_Note_MM'Img);
      Comment ("Distance for lowest frequency");
      New_Line (Output_File);
      Put_Line (Output_File, "typedef enum {");
      for R in Octave_Ranges loop
         Indent;
         Put (Output_File, R'Img & " =" & Octave_Ranges'Enum_Rep (R)'Img);
         if R = Octave_Ranges'Last then
            New_Line (Output_File);
         else
            Put_Line (Output_File, ",");
         end if; -- R = Octave_Ranges'Last
      end loop; -- R in Octave_Ranges
      Put_Line (Output_File, "} Octave_Ranges;");
      Put (Output_File, "#define Octave_Range_Count" &
             Natural'Image (Octave_Ranges'Pos (Octave_Ranges'Last) + 1));
      Comment ("Number of Octave_Ranges");
      New_Line (Output_File);
      Put (Output_File, "#define Loud_MM" & Loud_MM'Img);
      Comment ("Distance for maximum voume");
      Put (Output_File, "#define Mute_MM" & Muted_MM'Img);
      Comment ("Distance to mute output");
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
      Put_Line (Output_File, "int32_t Left;");
      Indent;
      Put_Line (Output_File, "int32_t Right;");
      Put_Line (Output_File, "} Audio_Sample;");
      New_Line (Output_File);
      Put_Line (Output_File, "#define Default_Waveform Sine_2");
      --  Sine_2 is entered as a string becayse it is mixed case. by drfault the
      --  'Img attribute returns an upper case string, the c language is case
      --  sensitive, so this would be a problem.
      Put_Line (Output_File, "#define Default_Octave_Range " &
                  Default_Octave_Range'Img);
      New_Line (Output_File);
      Comment ("This function returns a single sample of audio each time it");
      Comment ("is called. The sample rate must be" & Sample_Rate'Img
               & "Hz. Waveform");
      Comment ("specifies the type of waveform to be produced. Pitch_Distance");
      Comment ("specifies the frequency. The maximum frequency is produced at");
      Comment ("Highest_Note_MM and the minimum frequency is produced at");
      Comment ("Lowest_Note_MM. If the measured distance is out of range,");
      Comment ("or more than Lowest_Note_MM, set Distance to");
      Comment ("Lowest_Note_MM. If the measured distance is less than");
      Comment ("Highest_Note_MM then set Distance to Highest_Note_MM.");
      Comment ("Volume_Distance defines the output volume, for Loud_MM the");
      Comment ("the maximum output level is produced and for Muted_MM, no");
      Comment ("output is produced.");
      Comment ("Octave_Range specifies the range of frequencies playable for");
      Comment ("the range Highest_Note_MM to Lowest_Note_MM.");
      Comment ("Values are clamped to their respective valid ranges.");
      Put_Line (Output_File, "Audio_Sample DDS_Generator (");
      Indent;
      Put_Line (Output_File, "const Waveforms Waveform,");
      Indent;
      Put_Line (Output_File, "const int Pitch_Distance,");
      Indent;
      Put_Line (Output_File, "const int Volume_Distance,");
      Indent;
      Put_Line (Output_File, "const Octave_Ranges Octave_Range");
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
      Put_Line (Output_File, "#define " & Frequency_Count_String &
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
           "static const uint32_t __not_in_flash (""dds"") " &
            Phase_Step_String &
            " [" & Frequency_Count_String & "] = {");
      for F in Frequency_Indices loop
         if F mod 5 = 0 then
            New_Line (Output_File);
            Indent;
         end if; -- F mod 5 = 0
         Put (Output_File, Frequency_Table (F).Step, Width_32);
         if F /= Frequency_Indices'Last then
            Put (Output_File, ',');
         else
            New_Line (Output_File);
            Put (Output_File, "};");
            Comment (Phase_Step_String);
         end if; -- F /= Frequency_Indices'Last
      end loop; -- F in Frequency_Indices
      New_Line (Output_File);
      Comment ("Length of the first dimension of Wave_Table, the number of");
      Comment ("wavefforms.");
      Put_Line (Output_File, "#define " & Waveform_Count_String &
                Waveform_Count'Img );
      Comment ("Length of the second dimension of Wave_Table, the number");
      Comment ("of samples for each waveform.");
      Put_Line (Output_File, "#define Sample_Count" & Sample_Count'Img );
      New_Line (Output_File);
      Comment ("Number of bits the phase accumulator is shifted right to");
      Comment ("produce a Wave_Table index, 32 - log2 (Wave_Table length).");
      Put_Line (Output_File, "#define Phase_Shift" & Phase_Shift'Img);
      New_Line (Output_File);
      Comment ("The table represents the instantaneous value for each");
      Comment ("waveform for each value of phase.");
      Put_Line (Output_File,
                "static const int32_t __not_in_flash (""dds"") " &
                Wave_Table_String &
                " [" & Waveform_Count_String & "] [" &
                Sample_Count_String & "] = {");
      for W in Waveforms loop
         Indent;
         Put (Output_File, '{'); 
         Comment (W'Img);
         for S in Sample_Indices loop
            if S mod 4 = 0 then
               if S = Sample_Indices'First then
                  Indent (2);
               else
                  New_Line (Output_File);
                  Indent (2);
               end if; -- S = Sample_Indices'First
            end if; -- S mod 8 = 4
            Put (Output_File, Wave_Table (W) (S), Width_32);
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
      Comment (Wave_Table_String);
      New_Line (Output_File);
      Put (Output_File, "#define Volume_Count" &
      Positive'Image (Muted_MM - Loud_MM + 1));
      Comment ("Size of the volume table");
      New_Line (Output_File);
      Comment ("This table defines the volume multiplier for each distance");
      Put_Line (Output_File, "static const int32_t __not_in_flash (""dds"") " &
                Volume_Table_String & " [" & Volume_Count_String & "] = {");
      for V in Volume_Indices loop
         if V mod 4 = 0 then
            New_Line (Output_File);
            Indent;
         end if; -- V mod 4 = 0
         Put (Output_File, Volume_Table (V), Width_32);
         if V /= Volume_Indices'Last then
            Put (Output_File, ',');
         else
            New_Line (Output_File);
            Put (Output_File, "};");
            Comment (Volume_Table_String);
         end if; -- V /= Volume_Indices'Last
      end loop; -- V in Volume_Indices
      New_Line (Output_File);
      Put_Line (Output_File, "#endif // DDS_TABLE_H");
   end Write_Headers;

   Frequency_Table : Frequency_Tables;
   Wave_Table : Wave_Tables;
   Property_Table : Property_Tables;
   Volume_Table : Volume_Tables;

begin -- Optical_Theremin 
   Put_Line (Name_and_Version);
   Build (Frequency_Table);
   Put (Frequency_Table);
   --  The pitch ramp in the DDS generator takes the difference of two phase
   --  steps as an int32_t, so the largest shifted step must be less than
   --  2 ** 31.
   if Integer_64 (Frequency_Table (Frequency_Indices'First).Step) >=
     Integer_64 (Integer_32'Last)
   then
      raise Program_Error with
        "Largest Phase_Step cannot be represebted as a 32 bit integer";
   end if; -- Integer_64 (Frequency_Table (Frequency_Indices'First).Step) >= ...
   Build (Wave_Table);
   Put (Wave_Table);
   Wave_Properties (Wave_Table, Property_Table);
   Put (Property_Table);
   Build (Volume_Table);
   Put (Volume_Table);
   Write_Headers (Frequency_Table, Wave_Table, Volume_Table);
end Optical_Theremin;