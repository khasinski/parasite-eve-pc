/* Memory cards in the host harness: the default host directories bu00 and
 * bu10 in the working directory, always present (dc_card.c puts them on
 * VMUs). */
void DcCard_Init(void) {}
int DcCard_Present(int port) {
    (void)port;
    return 1;
}
