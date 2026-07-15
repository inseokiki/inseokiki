.PHONY: all phy ber clean clean-phy clean-ber

all: phy ber

phy:
	$(MAKE) -C PHY -f c_Makefile

ber:
	$(MAKE) -C BER

clean: clean-phy clean-ber

clean-phy:
	$(MAKE) -C PHY -f c_Makefile clean

clean-ber:
	$(MAKE) -C BER clean
