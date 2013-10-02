/*
 * islenefs: sistema de arquivos de brinquedo para utilização em sala de aula.
 * Versão original escrita por Glauber de Oliveira Costa para a disciplina MC514
 * (Sistemas Operacionais: teoria e prática) no primeiro semestre de 2008.
 * Código atualizado para rodar com kernel 3.10.x.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/vfs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/current.h>
#include <asm/uaccess.h>


/* O islenefs eh um fs muito simples. Os inodes sao atribuidos em ordem crescente, sem
 * reaproveitamento */
static int inode_number = 0;

/* Lembre-se que nao temos um disco! (Isso so complicaria as coisas, pois teriamos
 * que lidar com o sub-sistema de I/O. Entao teremos uma representacao bastante
 * simples da estrutura de arquivos: Uma lista duplamente ligada circular (para
 * aplicacoes reais, um hash seria muito mais adequado) contem em cada elemento
 * um indice (inode) e uma pagina para conteudo (ou seja: o tamanho maximo de um
 * arquivo nessa versao do islene fs eh de 4Kb. Nao ha subdiretorios */
struct file_contents {
	struct list_head list;
	struct inode *inode;
	void *conts;
};

/* lista duplamente ligada circular, contendo todos os arquivos do fs */
static LIST_HEAD(contents_list);

static const struct super_operations islenefs_ops = {
        .statfs         = simple_statfs,
        .drop_inode     = generic_delete_inode,
};

/* Lembram quando eu disse que um hash seria mais eficiente? ;-) */
static struct file_contents *islenefs_find_file(struct inode *inode)
{
	struct file_contents *f;
	list_for_each_entry(f, &contents_list, list) {
		if (f->inode == inode)
			return f;
	}
	return NULL;
}

/* Apos passar pelo VFS, uma leitura chegara aqui. A unica
 * coisa que fazemos eh, achar o ponteiro para o conteudo do arquivo,
 * e retornar, de acordo com o tamanho solicitado */
ssize_t islenefs_read(struct file *file, char __user *buf,
		      size_t count, loff_t *pos)
{
	struct file_contents *f;
	struct inode *inode = file->f_path.dentry->d_inode;
	int size = count;

	f = islenefs_find_file(inode);
	if (f == NULL)
		return -EIO;
		
	if (f->inode->i_size < count)
		size = f->inode->i_size;

	if ((*pos + size) >= f->inode->i_size)
		size = f->inode->i_size - *pos;
	
	/* As page tables do kernel estao sempre mapeadas (veremos o que
	 * sao page tables mais pra frente do curso), mas o mesmo nao eh
	 * verdade com as paginas de espaco de usuario. Dessa forma, uma
	 * atribuicao de/para um ponteiro contendo um endedereco de espaco
	 * de usuario pode falhar. Dessa forma, toda a comunicacao
	 * de/para espaco de usuario eh feita com as funcoes copy_from_user()
	 * e copy_to_user(). */
	if (copy_to_user(buf, f->conts + *pos, size))
		return -EFAULT;
	*pos += size;

	return size;
}

/* similar a leitura, mas vamos escrever no ponteiro do conteudo.
 * Por simplicidade, estamos escrevendo sempre no comeco do arquivo.
 * Obviamente, esse nao eh o comportamento esperado de um write 'normal'
 * Mas implementacoes de sistemas de arquivos sao flexiveis... */
ssize_t islenefs_write(struct file *file, const char __user *buf,
		       size_t count, loff_t *pos)
{
	struct file_contents *f;
	struct inode *inode = file->f_path.dentry->d_inode;

	f = islenefs_find_file(inode);
	if (f == NULL)
		return -ENOENT;
		
	/* copy_from_user() : veja comentario na funcao de leitura */
	if (copy_from_user(f->conts + *pos, buf, count))
		return -EFAULT;

	inode->i_size = count;

	return count;
}

static int islenefs_open(struct inode *inode, struct file *file)
{
	/* Todo arquivo tem uma estrutura privada associada a ele.
	 * Em geral, estamos apenas copiando a do inode, se houver. Mas isso
	 * eh tao flexivel quanto se queira, e podemos armazenar aqui
	 * qualquer tipo de coisa que seja por-arquivo. Por exemplo: Poderiamos
	 * associar um arquivo /mydir/4321 com o processo no 4321 e guardar aqui
	 * a estrutura que descreve este processo */
  printk ("islenefs: open!");
	if (inode->i_private)
		file->private_data = inode->i_private;
	return 0;
}

const struct file_operations islenefs_file_operations = {
	.read = islenefs_read,
	.write = islenefs_write,
	.open = islenefs_open,
};

static const struct inode_operations islenefs_file_inode_operations = {
	.getattr        = simple_getattr,
};

/* criacao de um arquivo: sem misterio, sem segredo, apenas
 * alocar as estruturas, preencher, e retornar */
static int islenefs_create (struct inode *dir, struct dentry * dentry,
			    umode_t mode, bool excl)
{


   	struct inode *inode;
	struct file_contents *file = kmalloc(sizeof(*file), GFP_KERNEL);	
	struct page *page;

	if (!file)
		return -EAGAIN;

	inode = new_inode(dir->i_sb);

	inode->i_mode = mode | S_IFREG;
//	inode->i_uid = current->cred->fsuid;
//	inode->i_gid = current->cred->fsgid;
	inode->i_blocks = 0;
//	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
        inode->i_ino = ++inode_number;

	inode->i_op = &islenefs_file_inode_operations;
	inode->i_fop = &islenefs_file_operations;

	file->inode = inode;
	page = alloc_page(GFP_KERNEL);
	if (!page)
		goto cleanup;

	file->conts = page_address(page);
	INIT_LIST_HEAD(&file->list);
	list_add_tail(&file->list, &contents_list); 
	d_instantiate(dentry, inode);  
	dget(dentry);

	return 0;
cleanup:
	iput(inode);
	kfree(file);
	return -EINVAL;
}


static const struct inode_operations islenefs_dir_inode_operations = {
        .create         = islenefs_create,
	.lookup		= simple_lookup,
};


static int islenefs_fill_super(struct super_block *sb, void *data, int silent)
{


struct inode * inode;
        struct dentry * root;

      //  sb->s_maxbytes = 4096;
	sb->s_magic = 0xBEBACAFE;
//	sb->s_blocksize = 1024;
//	sb->s_blocksize_bits = 10;

        sb->s_op = &islenefs_ops;
        sb->s_time_gran = 1;

        inode = new_inode(sb);

        if (!inode)
                return -ENOMEM;

        inode->i_ino = ++inode_number;
       // inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
        inode->i_blocks = 0;
       // inode->i_uid = inode->i_gid = 0;
        inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
        inode->i_op = &islenefs_dir_inode_operations;
        inode->i_fop = &simple_dir_operations;
       // set_nlink(inode, 2);

        root = d_make_root(inode);
        if (!root) {
                iput(inode);
                return -ENOMEM;
        }
        sb->s_root = root;
        return 0;

}

static struct dentry *islenefs_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name,
		   void *data)
{
  return mount_bdev(fs_type, flags, dev_name, data, islenefs_fill_super);
}

static struct file_system_type islenefs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "islenefs",
	.mount		= islenefs_get_sb,
	.kill_sb	= kill_litter_super,
};

static int __init init_islenefs_fs(void)
{
	INIT_LIST_HEAD(&contents_list);
	return register_filesystem(&islenefs_fs_type);
}

static void __exit exit_islenefs_fs(void)
{
	unregister_filesystem(&islenefs_fs_type);
}

module_init(init_islenefs_fs)
module_exit(exit_islenefs_fs)
MODULE_LICENSE("GPL");

