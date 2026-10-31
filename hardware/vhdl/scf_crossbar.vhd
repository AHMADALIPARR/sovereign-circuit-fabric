--------------------------------------------------------------------------------
-- Sovereign Circuit Fabric — Switching Crossbar (synthesizable VHDL)
-- Bounded, explicit timing, reset, and arbitration.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity scf_crossbar is
  generic (
    PORTS      : positive := 4;
    DATA_WIDTH : positive := 64;
    LABEL_WIDTH: positive := 32
  );
  port (
    clk          : in  std_logic;
    rst_n        : in  std_logic;

    -- Ingress ports
    in_valid     : in  std_logic_vector(PORTS-1 downto 0);
    in_data      : in  std_logic_vector(PORTS*DATA_WIDTH-1 downto 0);
    in_label     : in  std_logic_vector(PORTS*LABEL_WIDTH-1 downto 0);
    in_ready     : out std_logic_vector(PORTS-1 downto 0);

    -- Egress ports
    out_valid    : out std_logic_vector(PORTS-1 downto 0);
    out_data     : out std_logic_vector(PORTS*DATA_WIDTH-1 downto 0);
    out_ready    : in  std_logic_vector(PORTS-1 downto 0);

    -- Simple destination select (one-hot or encoded per ingress)
    dest_port    : in  std_logic_vector(PORTS*8-1 downto 0)  -- 8 bits per ingress
  );
end entity scf_crossbar;

architecture rtl of scf_crossbar is
  type data_array_t is array (0 to PORTS-1) of std_logic_vector(DATA_WIDTH-1 downto 0);
  signal in_data_arr  : data_array_t;
  signal out_data_arr : data_array_t;
  signal grant        : std_logic_vector(PORTS-1 downto 0);
begin

  -- Unpack inputs
  gen_unpack : for i in 0 to PORTS-1 generate
    in_data_arr(i) <= in_data((i+1)*DATA_WIDTH-1 downto i*DATA_WIDTH);
  end generate;

  -- Round-robin style arbitration (simplified deterministic priority)
  process (clk, rst_n)
    variable dest : integer range 0 to PORTS-1;
  begin
    if rst_n = '0' then
      out_valid <= (others => '0');
      grant     <= (others => '0');
      for i in 0 to PORTS-1 loop
        out_data_arr(i) <= (others => '0');
      end loop;
    elsif rising_edge(clk) then
      out_valid <= (others => '0');
      grant     <= (others => '0');
      for i in 0 to PORTS-1 loop
        if in_valid(i) = '1' then
          dest := to_integer(unsigned(dest_port((i+1)*8-1 downto i*8)));
          if dest < PORTS and out_ready(dest) = '1' then
            out_data_arr(dest) <= in_data_arr(i);
            out_valid(dest)    <= '1';
            grant(i)           <= '1';
          end if;
        end if;
      end loop;
    end if;
  end process;

  in_ready <= grant;

  gen_pack : for i in 0 to PORTS-1 generate
    out_data((i+1)*DATA_WIDTH-1 downto i*DATA_WIDTH) <= out_data_arr(i);
  end generate;

end architecture rtl;
